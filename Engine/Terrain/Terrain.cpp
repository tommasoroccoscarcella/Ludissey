#include "Terrain.hpp"

#include "Engine/Core/Text.hpp"
#include "Localization/Localization.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>

namespace ld {

std::string ChunkKey::text() const { return std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z); }

std::optional<ChunkKey> ChunkKey::parse(std::string_view text) {
    auto parts = split(text, ',');
    if (parts.size() != 3) return std::nullopt;
    ChunkKey key;
    int* targets[3] = {&key.x, &key.y, &key.z};
    for (int i = 0; i < 3; ++i) {
        const auto& part = parts[static_cast<std::size_t>(i)];
        if (part.empty() || part.size() > 9) return std::nullopt;
        std::size_t start = part[0] == '-' ? 1 : 0;
        if (start == part.size()) return std::nullopt;
        for (std::size_t c = start; c < part.size(); ++c)
            if (part[c] < '0' || part[c] > '9') return std::nullopt;
        *targets[i] = std::stoi(part);
    }
    return key;
}

std::string_view materialName(TerrainMaterial material) { return rules::terrain::materials[static_cast<std::size_t>(material)]; }
std::string_view materialColor(TerrainMaterial material) { return rules::terrain::colors[static_cast<std::size_t>(material)]; }

std::string materialTitle(TerrainMaterial material) {
    switch (material) {
    case TerrainMaterial::air: return tr("Aria");
    case TerrainMaterial::grass: return tr("Erba");
    case TerrainMaterial::rock: return tr("Roccia");
    case TerrainMaterial::sand: return tr("Sabbia");
    case TerrainMaterial::mud: return tr("Fango");
    case TerrainMaterial::water: return tr("Acqua");
    }
    return tr("Aria");
}

bool isSolid(TerrainMaterial material) { return material != TerrainMaterial::air && material != TerrainMaterial::water; }

Json TerrainSettings::json() const {
    return Json::object({{"seed", seed}, {"amplitude", amplitude}, {"baseHeight", baseHeight}, {"waterLevel", waterLevel}});
}

TerrainSettings TerrainSettings::fromJson(const Json& value) {
    TerrainSettings s;
    s.seed = static_cast<int>(value["seed"].integer(s.seed));
    s.amplitude = value["amplitude"].number(s.amplitude);
    s.baseHeight = value["baseHeight"].number(s.baseHeight);
    s.waterLevel = value["waterLevel"].number(s.waterLevel);
    return s;
}

Json TerrainWater::json() const {
    return Json::object({{"color", color}, {"transparency", transparency}, {"reflectance", reflectance}, {"waveSize", waveSize}, {"waveSpeed", waveSpeed}});
}

TerrainWater TerrainWater::fromJson(const Json& value) {
    TerrainWater w;
    if (!value.isObject()) return w;
    auto bounded = [&](const char* key, double fallback, double low, double high) {
        const Json& v = value[key];
        return v.isNumber() && std::isfinite(v.number()) ? std::clamp(v.number(), low, high) : fallback;
    };
    namespace water = rules::terrain::water;
    std::string color = value["color"].stringOr("");
    if (color.size() == 6 && std::all_of(color.begin(), color.end(), [](char c) { return std::isxdigit(static_cast<unsigned char>(c)); })) {
        for (char& c : color) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        w.color = color;
    }
    w.transparency = bounded("transparency", w.transparency, water::transparency::minimum, water::transparency::maximum);
    w.reflectance = bounded("reflectance", w.reflectance, water::reflectance::minimum, water::reflectance::maximum);
    w.waveSize = bounded("waveSize", w.waveSize, water::waveSize::minimum, water::waveSize::maximum);
    w.waveSpeed = bounded("waveSpeed", w.waveSpeed, water::waveSpeed::minimum, water::waveSpeed::maximum);
    return w;
}

bool TerrainRegion::valid() const {
    return std::isfinite(minimum.x) && std::isfinite(minimum.y) && std::isfinite(minimum.z) && std::isfinite(maximum.x) && std::isfinite(maximum.y) && std::isfinite(maximum.z) &&
        minimum.x < maximum.x && minimum.y < maximum.y && minimum.z < maximum.z;
}

TerrainRegion TerrainRegion::whole(double top) {
    Vec3 end = Terrain::origin + Vec3{Terrain::cellsX * Terrain::cellSize, Terrain::cellsY * Terrain::cellSize, Terrain::cellsZ * Terrain::cellSize};
    return snapped(Terrain::origin, {end.x, top, end.z});
}

TerrainRegion TerrainRegion::snapped(Vec3 low, Vec3 high) {
    const Vec3 o = Terrain::origin;
    const double cs = Terrain::cellSize;
    const Vec3 end = o + Vec3{Terrain::cellsX * cs, Terrain::cellsY * cs, Terrain::cellsZ * cs};
    auto cell = [&](double value, double start) { return start + std::round((value - start) / cs) * cs; };
    TerrainRegion r;
    r.minimum = {std::clamp(cell(std::min(low.x, high.x), o.x), o.x, end.x - cs), std::clamp(cell(std::min(low.y, high.y), o.y), o.y, end.y - cs), std::clamp(cell(std::min(low.z, high.z), o.z), o.z, end.z - cs)};
    r.maximum = {std::clamp(cell(std::max(low.x, high.x), o.x), r.minimum.x + cs, end.x), std::clamp(std::round(std::max(low.y, high.y) * 4) / 4, r.minimum.y + 0.25, end.y), std::clamp(cell(std::max(low.z, high.z), o.z), r.minimum.z + cs, end.z)};
    return r;
}

std::string_view toolName(TerrainTool tool) {
    switch (tool) {
    case TerrainTool::add: return "add";
    case TerrainTool::subtract: return "subtract";
    case TerrainTool::paint: return "paint";
    case TerrainTool::smooth: return "smooth";
    case TerrainTool::flatten: return "flatten";
    case TerrainTool::water: return "water";
    case TerrainTool::raise: return "raise";
    }
    return "add";
}

std::string toolTitle(TerrainTool tool) {
    switch (tool) {
    case TerrainTool::add: return tr("Aggiungi");
    case TerrainTool::subtract: return tr("Sottrai");
    case TerrainTool::paint: return tr("Dipingi");
    case TerrainTool::smooth: return tr("Leviga");
    case TerrainTool::flatten: return tr("Appiana");
    case TerrainTool::water: return tr("Acqua");
    case TerrainTool::raise: return tr("Rigonfia");
    }
    return tr("Aggiungi");
}

std::uint64_t Terrain::nextStamp() {
    static std::atomic<std::uint64_t> clock{0};
    return ++clock;
}

std::vector<ChunkKey> Terrain::keys() const {
    std::vector<ChunkKey> result;
    result.reserve(chunks.size());
    for (const auto& entry : chunks) result.push_back(entry.first);
    return result;
}

std::uint16_t Terrain::compose(TerrainMaterial solid, int solidLevel, int waterLevel) {
    int level = std::clamp(solidLevel, 0, levels);
    int wet = std::clamp(waterLevel, 0, levels);
    int bits = isSolid(solid) && level > 0 ? static_cast<int>(solid) << 4 | level : 0;
    return static_cast<std::uint16_t>(bits | wet << 8);
}

TerrainMaterial Terrain::material(std::uint16_t cell) {
    int raw = (cell >> 4) & 7;
    return raw <= static_cast<int>(TerrainMaterial::mud) ? static_cast<TerrainMaterial>(raw) : TerrainMaterial::air;
}

std::uint16_t Terrain::legacy(std::uint8_t byte) {
    int raw = byte >> 4, level = byte & 15;
    if (raw == static_cast<int>(TerrainMaterial::water)) return compose(TerrainMaterial::air, 0, level);
    return compose(raw <= static_cast<int>(TerrainMaterial::mud) ? static_cast<TerrainMaterial>(raw) : TerrainMaterial::air, level, 0);
}

std::uint16_t Terrain::cell(int i, int j, int k) const {
    if (!inside(i, j, k)) return 0;
    auto it = chunks.find(key(i, j, k));
    return it == chunks.end() ? 0 : it->second[static_cast<std::size_t>(offset(i, j, k))];
}

void Terrain::set(int i, int j, int k, std::uint16_t value) {
    if (!inside(i, j, k)) return;
    ChunkKey where = key(i, j, k);
    auto it = chunks.find(where);
    std::vector<std::uint16_t> values = it == chunks.end() ? std::vector<std::uint16_t>(volume, 0) : it->second;
    auto position = static_cast<std::size_t>(offset(i, j, k));
    if (values[position] == value) return;
    values[position] = value;
    if (std::all_of(values.begin(), values.end(), [](std::uint16_t v) { return v == 0; })) chunks.erase(where);
    else chunks[where] = std::move(values);
    stamps[where] = nextStamp();
}

void Terrain::clear() {
    for (const auto& entry : chunks) stamps[entry.first] = nextStamp();
    chunks.clear();
}

double Terrain::solidDensity(int i, int j, int k) const {
    std::uint16_t value = cell(i, j, k);
    return isSolid(material(value)) ? static_cast<double>(occupancy(value)) / levels : 0;
}

double Terrain::waterDensity(int i, int j, int k) const { return static_cast<double>(waterLevel(cell(i, j, k))) / levels; }

double Terrain::sample(double x, double y, double z, bool water) const {
    double fx = (x - origin.x) / cellSize - 0.5, fy = (y - origin.y) / cellSize - 0.5, fz = (z - origin.z) / cellSize - 0.5;
    int i = static_cast<int>(std::floor(fx)), j = static_cast<int>(std::floor(fy)), k = static_cast<int>(std::floor(fz));
    double tx = fx - i, ty = fy - j, tz = fz - k;
    auto d = [&](int a, int b, int c) { return water ? waterDensity(a, b, c) : solidDensity(a, b, c); };
    double c00 = d(i, j, k) * (1 - tx) + d(i + 1, j, k) * tx;
    double c10 = d(i, j + 1, k) * (1 - tx) + d(i + 1, j + 1, k) * tx;
    double c01 = d(i, j, k + 1) * (1 - tx) + d(i + 1, j, k + 1) * tx;
    double c11 = d(i, j + 1, k + 1) * (1 - tx) + d(i + 1, j + 1, k + 1) * tx;
    return (c00 * (1 - ty) + c10 * ty) * (1 - tz) + (c01 * (1 - ty) + c11 * ty) * tz;
}

std::optional<double> Terrain::surface(double x, double z, double top) const {
    if (empty() || top < origin.y) return std::nullopt;
    double step = cellSize / 8;
    double ceiling = origin.y + cellsY * cellSize;
    double y = std::min(top, ceiling);
    if (solid(x, y, z)) {
        if (top >= ceiling) return ceiling;
        return std::nullopt;
    }
    while (y > origin.y) {
        double below = y - step;
        if (solid(x, below, z)) {
            double low = below, high = y;
            for (int n = 0; n < 10; ++n) {
                double middle = (low + high) / 2;
                if (solid(x, middle, z)) low = middle;
                else high = middle;
            }
            return (low + high) / 2;
        }
        y = below;
    }
    return std::nullopt;
}

std::optional<double> Terrain::exit(double x, double z, double bottom) const {
    if (empty()) return std::nullopt;
    double step = cellSize / 8;
    double y = bottom;
    double ceiling = origin.y + cellsY * cellSize;
    while (y <= ceiling) {
        if (!solid(x, y, z)) {
            double low = std::max(bottom, y - step), high = y;
            for (int n = 0; n < 10; ++n) {
                double middle = (low + high) / 2;
                if (solid(x, middle, z)) low = middle;
                else high = middle;
            }
            return high;
        }
        y += step;
    }
    return ceiling;
}

std::optional<double> Terrain::raycast(Vec3 from, Vec3 direction, double length) const {
    if (empty()) return std::nullopt;
    double step = cellSize / 6;
    double travelled = 0;
    while (travelled <= length) {
        if (solid(from.x + direction.x * travelled, from.y + direction.y * travelled, from.z + direction.z * travelled)) return std::max(0.0, travelled - step / 2);
        travelled += step;
    }
    return std::nullopt;
}

double Terrain::hash(long long a, long long b, long long seed) {
    std::uint64_t value = static_cast<std::uint64_t>(a) * 0x9E3779B97F4A7C15ULL ^ static_cast<std::uint64_t>(b) * 0xC2B2AE3D27D4EB4FULL ^ static_cast<std::uint64_t>(seed) * 0x165667B19E3779F9ULL;
    value ^= value >> 31;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBULL;
    value ^= value >> 31;
    return static_cast<double>(value >> 11) / static_cast<double>(1ULL << 53);
}

double Terrain::noise(double x, double z, long long seed, int octaves) {
    double total = 0, weight = 0, amplitude = 1, spacing = 16;
    for (int octave = 0; octave < octaves; ++octave) {
        double gx = x / spacing, gz = z / spacing;
        auto i = static_cast<long long>(std::floor(gx)), k = static_cast<long long>(std::floor(gz));
        double tx = gx - static_cast<double>(i), tz = gz - static_cast<double>(k);
        double sx = tx * tx * (3 - 2 * tx), sz = tz * tz * (3 - 2 * tz);
        long long s = static_cast<long long>(static_cast<std::uint64_t>(seed) + static_cast<std::uint64_t>(octave) * 7919ULL);
        double top = hash(i, k, s) * (1 - sx) + hash(i + 1, k, s) * sx;
        double bottom = hash(i, k + 1, s) * (1 - sx) + hash(i + 1, k + 1, s) * sx;
        total += (top * (1 - sz) + bottom * sz) * amplitude;
        weight += amplitude;
        amplitude /= 2;
        spacing /= 2;
    }
    return total / weight;
}

void Terrain::generate(const TerrainSettings& chosen) {
    clear();
    settings = chosen;
    for (int i = 0; i < cellsX; ++i) {
        for (int k = 0; k < cellsZ; ++k) {
            Vec3 c = center(i, 0, k);
            double height = chosen.baseHeight + chosen.amplitude * (noise(c.x, c.z, chosen.seed, rules::terrain::generation::octaves) * 2 - 1);
            for (int j = 0; j < cellsY; ++j) {
                double bottom = origin.y + j * cellSize;
                double fraction = std::clamp((height - bottom) / cellSize, 0.0, 1.0);
                int level = static_cast<int>(std::round(fraction * levels));
                double depth = height - (bottom + cellSize);
                TerrainMaterial chosenMaterial = depth > cellSize * 1.5 ? TerrainMaterial::rock
                    : height < chosen.waterLevel + 1 ? (height < chosen.waterLevel - 1 ? TerrainMaterial::mud : TerrainMaterial::sand)
                    : TerrainMaterial::grass;
                int wet = bottom < chosen.waterLevel ? static_cast<int>(std::round(std::clamp((chosen.waterLevel - bottom) / cellSize, 0.0, 1.0) * levels)) : 0;
                set(i, j, k, compose(chosenMaterial, level, wet));
            }
        }
    }
}

namespace {

bool region(Vec3 minimum, Vec3 maximum) {
    return std::isfinite(minimum.x) && std::isfinite(minimum.y) && std::isfinite(minimum.z) && std::isfinite(maximum.x) && std::isfinite(maximum.y) && std::isfinite(maximum.z) &&
        minimum.x < maximum.x && minimum.y < maximum.y && minimum.z < maximum.z;
}

}

int Terrain::fillWater(Vec3 minimum, Vec3 maximum) {
    if (!region(minimum, maximum)) return 0;
    int changed = 0;
    for (int i = 0; i < cellsX; ++i) for (int j = 0; j < cellsY; ++j) for (int k = 0; k < cellsZ; ++k) {
        Vec3 c = center(i, j, k);
        double bottom = c.y - cellSize / 2;
        if (c.x < minimum.x || c.x >= maximum.x || c.z < minimum.z || c.z >= maximum.z || bottom < minimum.y - 1e-9 || bottom >= maximum.y) continue;
        auto before = cell(i, j, k);
        int wet = static_cast<int>(std::round(std::clamp((maximum.y - bottom) / cellSize, 0.0, 1.0) * levels));
        auto after = compose(material(before), occupancy(before), std::max(wet, waterLevel(before)));
        if (before != after) { set(i, j, k, after); ++changed; }
    }
    return changed;
}

int Terrain::clearWater(Vec3 minimum, Vec3 maximum) {
    if (!region(minimum, maximum)) return 0;
    int changed = 0;
    for (int i = 0; i < cellsX; ++i) for (int j = 0; j < cellsY; ++j) for (int k = 0; k < cellsZ; ++k) {
        Vec3 c = center(i, j, k);
        double bottom = c.y - cellSize / 2;
        if (c.x < minimum.x || c.x >= maximum.x || c.z < minimum.z || c.z >= maximum.z || bottom < minimum.y - 1e-9 || bottom >= maximum.y) continue;
        auto before = cell(i, j, k);
        auto after = compose(material(before), occupancy(before), 0);
        if (before != after) { set(i, j, k, after); ++changed; }
    }
    return changed;
}

int Terrain::fillRegion(Vec3 minimum, Vec3 maximum, TerrainMaterial fill) {
    if (fill == TerrainMaterial::water) return fillWater(minimum, maximum);
    if (!region(minimum, maximum) || !isSolid(fill)) return 0;
    int changed = 0;
    for (int i = 0; i < cellsX; ++i) for (int j = 0; j < cellsY; ++j) for (int k = 0; k < cellsZ; ++k) {
        Vec3 c = center(i, j, k);
        double bottom = c.y - cellSize / 2;
        if (c.x < minimum.x || c.x >= maximum.x || c.z < minimum.z || c.z >= maximum.z || bottom < minimum.y - 1e-9 || bottom >= maximum.y) continue;
        auto before = cell(i, j, k);
        int level = static_cast<int>(std::round(std::clamp((maximum.y - bottom) / cellSize, 0.0, 1.0) * levels));
        auto after = compose(fill, std::max(level, occupancy(before)), waterLevel(before));
        if (before != after) { set(i, j, k, after); ++changed; }
    }
    return changed;
}

int Terrain::clearRegion(Vec3 minimum, Vec3 maximum) {
    if (!region(minimum, maximum)) return 0;
    int changed = 0;
    for (int i = 0; i < cellsX; ++i) for (int j = 0; j < cellsY; ++j) for (int k = 0; k < cellsZ; ++k) {
        Vec3 c = center(i, j, k);
        double bottom = c.y - cellSize / 2;
        if (c.x < minimum.x || c.x >= maximum.x || c.z < minimum.z || c.z >= maximum.z || bottom < minimum.y - 1e-9 || bottom >= maximum.y) continue;
        if (cell(i, j, k) != 0) { set(i, j, k, 0); ++changed; }
    }
    return changed;
}

std::optional<TerrainMaterial> Terrain::dominant(const Terrain& terrain, int i, int j, int k) {
    std::map<TerrainMaterial, int> counts;
    constexpr int offsets[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const auto& o : offsets) {
        TerrainMaterial m = material(terrain.cell(i + o[0], j + o[1], k + o[2]));
        if (isSolid(m)) counts[m] += 1;
    }
    if (counts.empty()) return std::nullopt;
    auto best = counts.begin();
    for (auto it = counts.begin(); it != counts.end(); ++it) {
        bool better = it->second > best->second || (it->second == best->second && static_cast<int>(it->first) < static_cast<int>(best->first));
        if (better) best = it;
    }
    return best->first;
}

Result<int> Terrain::fillSphere(Vec3 point, double radius, TerrainMaterial fill) {
    if (!std::isfinite(point.x) || !std::isfinite(point.y) || !std::isfinite(point.z) || !std::isfinite(radius) || radius <= 0 || radius > rules::terrain::brush::maximum)
        return fail(ErrorCode::invalidArgument, "fillSphere: centro finito e raggio positivo entro " + std::to_string(rules::terrain::brush::maximum) + " richiesti");
    if (static_cast<unsigned>(fill) > static_cast<unsigned>(TerrainMaterial::water))
        return fail(ErrorCode::invalidArgument, "fillSphere: materiale non valido");
    bool wet = fill == TerrainMaterial::water;
    const Vec3 end = origin + Vec3{cellsX * cellSize, cellsY * cellSize, cellsZ * cellSize};
    if (point.x - radius > end.x || point.y - radius > end.y || point.z - radius > end.z || point.x + radius < origin.x || point.y + radius < origin.y || point.z + radius < origin.z) return 0;
    int fromI = index(std::clamp(point.x - radius, origin.x, end.x), origin.x);
    int fromJ = index(std::clamp(point.y - radius, origin.y, end.y), origin.y);
    int fromK = index(std::clamp(point.z - radius, origin.z, end.z), origin.z);
    int toI = std::min(cellsX - 1, index(std::clamp(point.x + radius, origin.x, end.x), origin.x));
    int toJ = std::min(cellsY - 1, index(std::clamp(point.y + radius, origin.y, end.y), origin.y));
    int toK = std::min(cellsZ - 1, index(std::clamp(point.z + radius, origin.z, end.z), origin.z));
    int changed = 0;
    for (int i = fromI; i <= toI; ++i) for (int j = fromJ; j <= toJ; ++j) for (int k = fromK; k <= toK; ++k) {
        double distance = (center(i, j, k) - point).length();
        if (distance > radius) continue;
        int level = static_cast<int>(std::round(std::clamp((radius - distance) / cellSize + 0.5, 0.0, 1.0) * levels));
        auto before = cell(i, j, k);
        auto value = wet ? compose(material(before), occupancy(before), std::max(level, waterLevel(before))) : compose(fill, level, waterLevel(before));
        if (before != value) { set(i, j, k, value); ++changed; }
    }
    return changed;
}

Vec3 Terrain::snapped(Vec3 point) { return {origin.x + (index(point.x, origin.x) + 0.5) * cellSize, point.y, origin.z + (index(point.z, origin.z) + 0.5) * cellSize}; }

std::optional<double> Terrain::surfaceHeight(int i, int k) const {
    for (int j = cellsY - 1; j >= 0; --j) {
        std::uint16_t value = cell(i, j, k);
        if (isSolid(material(value)) && occupancy(value) > 0) return origin.y + j * cellSize + static_cast<double>(occupancy(value)) / levels * cellSize;
    }
    return std::nullopt;
}

int Terrain::columns(TerrainTool tool, Vec3 point, const TerrainBrush& brush) {
    double radius = std::clamp(brush.radius, static_cast<double>(rules::terrain::brush::minimum), static_cast<double>(rules::terrain::brush::maximum));
    double step = std::clamp(brush.strength, 0.05, 1.0) * cellSize;
    bool surface = brush.placement == TerrainPlacement::surface;
    Vec3 aim = surface ? snapped(point) : point;
    int fromI = std::max(0, index(aim.x - radius, origin.x)), toI = std::min(cellsX - 1, index(aim.x + radius, origin.x));
    int fromK = std::max(0, index(aim.z - radius, origin.z)), toK = std::min(cellsZ - 1, index(aim.z + radius, origin.z));
    if (fromI > toI || fromK > toK) return 0;
    TerrainMaterial fill = isSolid(brush.material) ? brush.material : TerrainMaterial::grass;
    int changed = 0;
    for (int i = fromI; i <= toI; ++i)
        for (int k = fromK; k <= toK; ++k) {
            Vec3 c = center(i, 0, k);
            double distance = std::hypot(c.x - aim.x, c.z - aim.z);
            if (distance > radius) continue;
            auto current = surfaceHeight(i, k);
            double weight = tool == TerrainTool::raise ? std::max(0.0, 1 - (distance / radius) * (distance / radius)) : 1.0;
            double floor = current ? -1e9 : aim.y - cellSize / 2;
            double target = 0;
            bool lowering = tool == TerrainTool::subtract;
            if (tool == TerrainTool::raise) target = (current ? *current : aim.y) + step * weight;
            else if (lowering) target = surface ? aim.y - step : (current ? *current : aim.y) - step;
            else target = surface ? aim.y + step : (current ? *current : aim.y) + step;
            if (lowering && !current) continue;
            for (int j = 0; j < cellsY; ++j) {
                double bottom = origin.y + j * cellSize;
                if (!lowering && bottom + cellSize <= floor) continue;
                int desired = static_cast<int>(std::round(std::clamp((target - bottom) / cellSize, 0.0, 1.0) * levels));
                std::uint16_t value = cell(i, j, k);
                TerrainMaterial here = material(value);
                int level = isSolid(here) ? occupancy(value) : 0;
                int wet = waterLevel(value);
                std::uint16_t next = value;
                if (lowering) {
                    if ((isSolid(here) && desired < level) || desired < wet) next = compose(here, std::min(level, desired), std::min(wet, desired));
                } else if (desired > level) next = compose(isSolid(here) ? here : fill, desired, wet);
                if (next != value) {
                    set(i, j, k, next);
                    ++changed;
                }
            }
        }
    return changed;
}

int Terrain::apply(TerrainTool tool, Vec3 point, const TerrainBrush& brush) {
    bool watering = tool == TerrainTool::water || (tool == TerrainTool::add && brush.material == TerrainMaterial::water);
    if (watering) tool = TerrainTool::water;
    if (!watering && (tool == TerrainTool::raise || (brush.placement == TerrainPlacement::surface && (tool == TerrainTool::add || tool == TerrainTool::subtract)))) return columns(tool, point, brush);
    double radius = std::clamp(brush.radius, static_cast<double>(rules::terrain::brush::minimum), static_cast<double>(rules::terrain::brush::maximum));
    double strength = std::clamp(brush.strength, 0.05, 1.0);
    int lowI = index(point.x - radius, origin.x), lowJ = index(point.y - radius, origin.y), lowK = index(point.z - radius, origin.z);
    int highI = index(point.x + radius, origin.x), highJ = index(point.y + radius, origin.y), highK = index(point.z + radius, origin.z);
    if (lowI >= cellsX || lowJ >= cellsY || lowK >= cellsZ || highI < 0 || highJ < 0 || highK < 0) return 0;
    int fromI = std::max(0, lowI), toI = std::max(fromI, std::min(cellsX - 1, highI));
    int fromJ = std::max(0, lowJ), toJ = std::max(fromJ, std::min(cellsY - 1, highJ));
    int fromK = std::max(0, lowK), toK = std::max(fromK, std::min(cellsZ - 1, highK));
    Terrain snapshot = *this;
    int changed = 0;
    for (int i = fromI; i <= toI; ++i) {
        for (int j = fromJ; j <= toJ; ++j) {
            for (int k = fromK; k <= toK; ++k) {
                Vec3 c = center(i, j, k);
                double distance = std::sqrt((c.x - point.x) * (c.x - point.x) + (c.y - point.y) * (c.y - point.y) + (c.z - point.z) * (c.z - point.z));
                if (distance > radius) continue;
                double falloff = std::max(0.0, 1 - (distance / radius) * (distance / radius));
                int amount = std::max(1, static_cast<int>(std::round(strength * falloff * levels)));
                std::uint16_t value = snapshot.cell(i, j, k);
                TerrainMaterial current = material(value);
                int level = occupancy(value);
                int wet = waterLevel(value);
                std::uint16_t next = value;
                switch (tool) {
                case TerrainTool::add: next = compose(isSolid(current) ? current : isSolid(brush.material) ? brush.material : TerrainMaterial::grass, level + amount, wet); break;
                case TerrainTool::subtract: next = compose(current, level - amount, wet - amount); break;
                case TerrainTool::paint:
                    if (isSolid(current) && isSolid(brush.material)) next = compose(brush.material, level, wet);
                    break;
                case TerrainTool::smooth: {
                    constexpr int offsets[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                    double sum = 0;
                    for (const auto& o : offsets) sum += snapshot.solidDensity(i + o[0], j + o[1], k + o[2]);
                    double average = (sum + snapshot.solidDensity(i, j, k)) / 7;
                    double here = snapshot.solidDensity(i, j, k);
                    double target = here + (average - here) * strength * falloff;
                    TerrainMaterial fill = isSolid(current) ? current : dominant(snapshot, i, j, k).value_or(brush.material);
                    int blended = static_cast<int>(std::round(target * levels));
                    next = compose(fill, blended, wet);
                    break;
                }
                case TerrainTool::flatten: {
                    double bottom = c.y - cellSize / 2;
                    double desired = std::clamp((point.y - bottom) / cellSize, 0.0, 1.0) * levels;
                    double here = isSolid(current) ? level : 0;
                    int blended = static_cast<int>(std::round(here + (desired - here) * std::min(1.0, strength * falloff * 2)));
                    next = compose(isSolid(current) ? current : isSolid(brush.material) ? brush.material : TerrainMaterial::grass, blended, wet);
                    break;
                }
                case TerrainTool::water: next = compose(current, level, wet + amount); break;
                case TerrainTool::raise: break;
                }
                if (next != value) {
                    set(i, j, k, next);
                    ++changed;
                }
            }
        }
    }
    return changed;
}

std::string Terrain::encodeChunk(const std::vector<std::uint16_t>& values) {
    std::string runs;
    std::size_t position = 0;
    while (position < values.size()) {
        std::uint16_t value = values[position];
        std::size_t count = 1;
        while (position + count < values.size() && values[position + count] == value && count < 255) ++count;
        runs += static_cast<char>(count);
        runs += static_cast<char>(value & 0xFF);
        runs += static_cast<char>(value >> 8);
        position += count;
    }
    return base64Encode(runs);
}

Result<std::vector<std::uint16_t>> Terrain::decodeChunk(std::string_view text, int format) {
    auto data = base64Decode(text);
    std::size_t width = format >= 2 ? 3 : 2;
    if (!data || data->size() % width != 0 || data->empty()) return fail(ErrorCode::corrupted, "Terreno corrotto: chunk non decodificabile");
    std::vector<std::uint16_t> values;
    values.reserve(volume);
    for (std::size_t run = 0; run < data->size(); run += width) {
        int count = (*data)[run];
        std::uint16_t value = 0;
        if (width == 3) {
            value = static_cast<std::uint16_t>((*data)[run + 1] | (*data)[run + 2] << 8);
            int raw = (value >> 4) & 7;
            if ((value & 0xF080) != 0 || raw > static_cast<int>(TerrainMaterial::mud) || (raw == 0) != ((value & 15) == 0)) return fail(ErrorCode::corrupted, "Terreno corrotto: cella non valida");
        } else {
            std::uint8_t byte = (*data)[run + 1];
            if ((byte >> 4) >= 6 || ((byte >> 4) == 0) != ((byte & 15) == 0)) return fail(ErrorCode::corrupted, "Terreno corrotto: cella non valida");
            value = legacy(byte);
        }
        if (count <= 0) return fail(ErrorCode::corrupted, "Terreno corrotto: cella non valida");
        if (values.size() + static_cast<std::size_t>(count) > static_cast<std::size_t>(volume)) return fail(ErrorCode::corrupted, "Terreno corrotto: dimensione del chunk errata");
        values.insert(values.end(), static_cast<std::size_t>(count), value);
    }
    if (values.size() != static_cast<std::size_t>(volume)) return fail(ErrorCode::corrupted, "Terreno corrotto: dimensione del chunk errata");
    return values;
}

Json Terrain::json() const {
    Json out = Json::object();
    out.set("format", rules::terrain::format);
    if (settings) out.set("settings", settings->json());
    out.set("water", waterProperties.json());
    Json raw = Json::object();
    for (const auto& [key, values] : chunks) raw.set(key.text(), encodeChunk(values));
    out.set("chunks", raw);
    return out;
}

Result<Terrain> Terrain::fromJson(const Json& value) {
    Terrain terrain;
    if (value.isNull()) return terrain;
    if (!value.isObject()) return fail(ErrorCode::corrupted, "Terreno corrotto: formato non valido");
    int format = 1;
    if (const Json* f = value.find("format")) {
        if (!f->isNumber() || f->number() != rules::terrain::format) return fail(ErrorCode::corrupted, "Terreno corrotto: formato non valido");
        format = rules::terrain::format;
    }
    if (const Json* s = value.find("settings"); s && s->isObject()) terrain.settings = TerrainSettings::fromJson(*s);
    if (const Json* w = value.find("water")) terrain.waterProperties = TerrainWater::fromJson(*w);
    if (const Json* raw = value.find("chunks")) {
        if (!raw->isObject()) return fail(ErrorCode::corrupted, "Terreno corrotto: chunk non validi");
        for (const auto& [text, payload] : raw->members()) {
            auto key = ChunkKey::parse(text);
            if (!key || key->x < 0 || key->y < 0 || key->z < 0 || key->x >= chunksX || key->y >= chunksY || key->z >= chunksZ)
                return fail(ErrorCode::corrupted, "Terreno corrotto: chunk fuori dai limiti");
            if (!payload.isString()) return fail(ErrorCode::corrupted, "Terreno corrotto: chunk non decodificabile");
            auto decoded = decodeChunk(payload.string(), format);
            if (!decoded) return decoded.error();
            if (std::any_of(decoded->begin(), decoded->end(), [](std::uint16_t v) { return v != 0; })) {
                terrain.chunks[*key] = std::move(*decoded);
                terrain.stamps[*key] = nextStamp();
            }
        }
    }
    return terrain;
}

std::uint64_t Terrain::stamp(const ChunkKey& key) const {
    auto it = stamps.find(key);
    return it == stamps.end() ? 0 : it->second;
}

std::uint64_t Terrain::neighbourhood(const ChunkKey& key) const {
    std::uint64_t h = 1469598103934665603ULL;
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dz = -1; dz <= 1; ++dz) {
                ChunkKey other{key.x + dx, key.y + dy, key.z + dz};
                std::uint64_t value = chunks.count(other) ? stamp(other) + 1 : 0;
                h = (h ^ value) * 1099511628211ULL;
            }
    return h;
}

}
