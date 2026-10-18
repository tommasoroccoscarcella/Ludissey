#include "World.hpp"

#include "Engine/Assets/Mesh.hpp"
#include "Engine/Backend/Assets.hpp"
#include "Engine/Core/Async.hpp"
#include "Engine/Core/Json.hpp"
#include "Engine/Core/Resources.hpp"
#include "Engine/Rendering/Renderer.hpp"
#include "Engine/Terrain/Mesher.hpp"

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <zlib.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>

namespace ld::render {

namespace {

std::vector<float> floats(const Json& value) {
    std::vector<float> out;
    out.reserve(value.size());
    for (const auto& v : value.items()) out.push_back(static_cast<float>(v.number()));
    return out;
}

CharacterFrame frame(const Json& value) { return {floats(value["vertices"]), floats(value["normals"])}; }

Bounds frameBounds(const CharacterFrame& f) {
    if (f.vertices.size() < 3) return {{0, 0, 0}, {1, 1, 1}};
    Vec3 low{f.vertices[0], f.vertices[1], f.vertices[2]}, high = low;
    for (std::size_t i = 0; i + 2 < f.vertices.size(); i += 3) {
        low = {std::min(low.x, static_cast<double>(f.vertices[i])), std::min(low.y, static_cast<double>(f.vertices[i + 1])), std::min(low.z, static_cast<double>(f.vertices[i + 2]))};
        high = {std::max(high.x, static_cast<double>(f.vertices[i])), std::max(high.y, static_cast<double>(f.vertices[i + 1])), std::max(high.z, static_cast<double>(f.vertices[i + 2]))};
    }
    return {low, high};
}

GeometryPtr unit(const std::string& kind) {
    static std::mutex lock;
    static std::map<std::string, GeometryPtr> cache;
    std::lock_guard<std::mutex> guard(lock);
    if (auto it = cache.find(kind); it != cache.end()) return it->second;
    GeometryPtr g;
    if (kind == "sphere") g = shapes::sphere(0.5);
    else if (kind == "cylinder") g = shapes::cylinder(0.5, 1);
    else if (kind == "wedge") g = shapes::wedge();
    else g = shapes::box(1, 1, 1, 0);
    cache[kind] = g;
    return g;
}

Material missingAsset() {
    Material m;
    m.diffuse = Color::hex(rules::part::color);
    m.lighting = Lighting::constant;
    m.wireframe = true;
    m.doubleSided = true;
    return m;
}

Material terrainMaterial(TerrainMaterial kind) {
    Material m;
    m.lighting = Lighting::terrain;
    m.terrainKind = kind == TerrainMaterial::grass || kind == TerrainMaterial::rock || kind == TerrainMaterial::sand || kind == TerrainMaterial::mud ? static_cast<int>(kind) : static_cast<int>(TerrainMaterial::rock);
    return m;
}

Material waterMaterial(const TerrainWater& water) {
    Material m;
    m.lighting = Lighting::water;
    m.diffuse = Color::hex(water.color, Color::hex(rules::terrain::water::color, Color{0.24, 0.5, 0.84, 1}));
    m.opacity = 1;
    m.doubleSided = false;
    m.writesDepth = true;
    m.readsDepth = true;
    m.transparency = water.transparency;
    m.reflectance = water.reflectance;
    m.waveSize = water.waveSize;
    m.waveSpeed = water.waveSpeed;
    return m;
}

bool openSea(const Terrain& terrain) {
    int columns = 0, wet = 0;
    auto column = [&](int i, int k) {
        ++columns;
        for (int j = Terrain::cellsY - 1; j >= 0; --j) {
            std::uint16_t value = terrain.cell(i, j, k);
            bool solid = isSolid(Terrain::material(value)) && Terrain::occupancy(value) * 2 >= Terrain::levels;
            if (solid) return;
            if (Terrain::waterLevel(value) > 0) {
                ++wet;
                return;
            }
        }
    };
    for (int i = 0; i < Terrain::cellsX; ++i) {
        column(i, 0);
        column(i, Terrain::cellsZ - 1);
    }
    for (int k = 1; k + 1 < Terrain::cellsZ; ++k) {
        column(0, k);
        column(Terrain::cellsX - 1, k);
    }
    return wet * 2 >= columns;
}

void addSeaRing(Geometry& g, const TerrainMesh& mesh) {
    double lowX = Terrain::origin.x, highX = -Terrain::origin.x, lowZ = Terrain::origin.z, highZ = -Terrain::origin.z;
    std::map<double, int> heights;
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        const Vec3& p = mesh.positions[i];
        bool rim = std::abs(p.x - lowX) < 1e-6 || std::abs(p.x - highX) < 1e-6 || std::abs(p.z - lowZ) < 1e-6 || std::abs(p.z - highZ) < 1e-6;
        if (rim && mesh.normals[i].y > 0.5) ++heights[std::round(p.y * 1024) / 1024];
    }
    if (heights.empty()) return;
    double level = std::max_element(heights.begin(), heights.end(), [](const auto& a, const auto& b) { return a.second < b.second; })->first;
    double reach = rules::terrain::water::seaRing, inset = Terrain::cellSize;
    level -= 0.02;
    auto rectangle = [&](double x0, double z0, double x1, double z1) {
        auto first = static_cast<std::uint32_t>(g.positions.size() / 3);
        for (auto [x, z] : {std::pair{x0, z0}, std::pair{x0, z1}, std::pair{x1, z1}, std::pair{x1, z0}}) {
            g.positions.insert(g.positions.end(), {static_cast<float>(x), static_cast<float>(level), static_cast<float>(z)});
            g.normals.insert(g.normals.end(), {0.0f, 1.0f, 0.0f});
            g.colors.insert(g.colors.end(), {1.0f, 1.0f, 1.0f});
        }
        g.indices.insert(g.indices.end(), {first, first + 1, first + 2, first, first + 2, first + 3});
    };
    rectangle(lowX - reach, highZ - inset, highX + reach, highZ + reach);
    rectangle(lowX - reach, lowZ - reach, highX + reach, lowZ + inset);
    rectangle(lowX - reach, lowZ + inset, lowX + inset, highZ - inset);
    rectangle(highX - inset, lowZ + inset, highX + reach, highZ - inset);
}

double extent(const Bounds& b) { return std::max({b.high.x - b.low.x, b.high.y - b.low.y, b.high.z - b.low.z}); }

}

CharacterFrame CharacterFrame::blended(const CharacterFrame& other, float weight) const {
    CharacterFrame out;
    std::size_t n = std::min(vertices.size(), other.vertices.size()), m = std::min(normals.size(), other.normals.size());
    out.vertices.resize(n);
    out.normals.resize(m);
    for (std::size_t i = 0; i < n; ++i) out.vertices[i] = vertices[i] + (other.vertices[i] - vertices[i]) * weight;
    for (std::size_t i = 0; i < m; ++i) out.normals[i] = normals[i] + (other.normals[i] - normals[i]) * weight;
    return out;
}

std::vector<CharacterFrame> CharacterClip::sample(double phase) const {
    if (frames.empty()) return {};
    double position = std::clamp(phase, 0.0, 1.0) * static_cast<double>(frames.size() - 1);
    std::size_t low = static_cast<std::size_t>(position), high = std::min(frames.size() - 1, low + 1);
    std::vector<CharacterFrame> out;
    for (std::size_t i = 0; i < std::min(frames[low].size(), frames[high].size()); ++i) out.push_back(frames[low][i].blended(frames[high][i], static_cast<float>(position - static_cast<double>(low))));
    return out;
}

std::shared_ptr<const CharacterAsset> CharacterAsset::load(const std::filesystem::path& path) {
    static std::mutex lock;
    static std::map<std::string, std::shared_ptr<const CharacterAsset>> cache;
    std::lock_guard<std::mutex> guard(lock);
    if (auto it = cache.find(path.string()); it != cache.end()) return it->second;
    std::ifstream stream(path, std::ios::binary);
    std::string packed((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (packed.size() <= 4) return nullptr;
    std::size_t size = 0;
    for (int i = 0; i < 4; ++i) size |= static_cast<std::size_t>(static_cast<unsigned char>(packed[static_cast<std::size_t>(i)])) << (i * 8);
    if (size == 0 || size >= 32000000) return nullptr;
    std::string data(size, '\0');
    z_stream z{};
    z.next_in = reinterpret_cast<Bytef*>(packed.data() + 4);
    z.avail_in = static_cast<uInt>(packed.size() - 4);
    z.next_out = reinterpret_cast<Bytef*>(data.data());
    z.avail_out = static_cast<uInt>(size);
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) return nullptr;
    int status = inflate(&z, Z_FINISH);
    std::size_t produced = z.total_out;
    inflateEnd(&z);
    if (status != Z_STREAM_END || produced != size) return nullptr;
    auto parsed = Json::parse(data, 64);
    if (!parsed.ok()) return nullptr;
    const Json& root = *parsed;
    auto asset = std::make_shared<CharacterAsset>();
    for (const auto& n : root["names"].items()) asset->names.push_back(n.string());
    for (const auto& f : root["rest"].items()) asset->rest.push_back(frame(f));
    for (const auto& [name, clip] : root["clips"].members()) {
        CharacterClip c;
        c.duration = clip["duration"].number(1);
        for (const auto& step : clip["frames"].items()) {
            std::vector<CharacterFrame> frames;
            for (const auto& f : step.items()) frames.push_back(frame(f));
            c.frames.push_back(std::move(frames));
        }
        asset->clips[name] = std::move(c);
    }
    if (asset->names.size() != asset->rest.size() || asset->rest.empty()) return nullptr;
    cache[path.string()] = asset;
    return asset;
}

namespace face {

std::filesystem::path chosen(const AvatarConfiguration& avatar) {
    std::filesystem::path folder = resourceFolder() / "Faces";
    auto catalog = ld::avatar_catalog::items();
    for (const auto& e : avatar.equipped)
        if (auto it = catalog.find(e.itemID); it != catalog.end() && it->second.category.name == "face" && it->second.asset)
            if (std::filesystem::exists(folder / *it->second.asset)) return folder / *it->second.asset;
    return folder / (avatar.bodyPreset == "female" ? "Female.png" : "Male.png");
}

std::shared_ptr<Texture> texture(const std::filesystem::path& path) {
    static std::mutex lock;
    static std::map<std::string, std::shared_ptr<Texture>> cache;
    std::lock_guard<std::mutex> guard(lock);
    if (auto it = cache.find(path.string()); it != cache.end()) return it->second;
    std::shared_ptr<Texture> result;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()), static_cast<CFIndex>(path.string().size()), false);
    CGImageSourceRef source = url ? CGImageSourceCreateWithURL(url, nullptr) : nullptr;
    CGImageRef image = source && CGImageSourceGetCount(source) > 0 ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
    if (image) {
        int width = static_cast<int>(CGImageGetWidth(image)), height = static_cast<int>(CGImageGetHeight(image));
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width * height * 4), 0);
        CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef context = CGBitmapContextCreate(rgba.data(), static_cast<std::size_t>(width), static_cast<std::size_t>(height), 8, static_cast<std::size_t>(width * 4), space, static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big);
        if (context) {
            CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
            CGContextRelease(context);
            for (std::size_t i = 0; i < rgba.size(); i += 4) {
                int alpha = rgba[i + 3];
                if (alpha == 0 || alpha == 255) continue;
                for (int c = 0; c < 3; ++c) rgba[i + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(std::min(255, (rgba[i + static_cast<std::size_t>(c)] * 255 + alpha / 2) / alpha));
            }
            result = Texture::make(std::move(rgba), width, height);
        }
        CGColorSpaceRelease(space);
        CGImageRelease(image);
    }
    if (source) CFRelease(source);
    if (url) CFRelease(url);
    cache[path.string()] = result;
    return result;
}

std::vector<float> uvs(const CharacterFrame& head) {
    Bounds b = frameBounds(head);
    Vec3 centre = b.center(), size = b.extent();
    double w = width * size.x, h = w;
    double cx = centre.x, cy = centre.y - drop * size.y;
    std::size_t count = head.vertices.size() / 3;
    std::vector<float> out(count * 2, -2);
    for (std::size_t v = 0; v < count; ++v) {
        double x = head.vertices[v * 3], y = head.vertices[v * 3 + 1], z = head.vertices[v * 3 + 2];
        double nz = v * 3 + 2 < head.normals.size() ? head.normals[v * 3 + 2] : 0;
        if (z <= centre.z || nz <= 0.05 || w <= 0) continue;
        out[v * 2] = static_cast<float>(0.5 + (x - cx) / w);
        out[v * 2 + 1] = static_cast<float>(0.5 - (y - cy) / h);
    }
    for (std::size_t t = 0; t + 2 < count; t += 3) {
        bool outside = false;
        for (std::size_t c = t; c < t + 3; ++c) outside = outside || out[c * 2] == -2;
        if (outside)
            for (std::size_t c = t; c < t + 3; ++c) out[c * 2] = out[c * 2 + 1] = -2;
    }
    return out;
}

}

CharacterModel::CharacterModel(std::shared_ptr<const CharacterAsset> source, const AvatarConfiguration& look) : asset(std::move(source)) {
    for (std::size_t i = 0; i < asset->names.size(); ++i) {
        auto g = shapes::triangles(asset->rest[i].vertices, asset->rest[i].normals);
        auto mesh = Node::make(g, asset->names[i]);
        node->add(mesh);
        meshes.push_back(mesh);
        if (asset->names[i].rfind("Head_", 0) == 0) faceUVs[i] = face::uvs(asset->rest[i]);
    }
    current = asset->rest;
    dress(look);
    animate("Idle", 0);
}

void CharacterModel::animate(const std::string& next, double dt, std::optional<double> phase) {
    auto it = asset->clips.find(next);
    if (it == asset->clips.end()) return;
    const CharacterClip& clip = it->second;
    if (state != next) {
        state = next;
        elapsed = 0;
        transition = 0;
        transitionFrames = current;
    }
    elapsed += dt;
    transition += dt;
    double progress = phase ? *phase : std::fmod(elapsed, clip.duration) / clip.duration;
    auto frames = clip.sample(progress);
    if (transition < 0.10 && transitionFrames.size() == frames.size())
        for (std::size_t i = 0; i < frames.size(); ++i) frames[i] = transitionFrames[i].blended(frames[i], static_cast<float>(transition / 0.10));
    for (std::size_t i = 0; i < std::min(meshes.size(), frames.size()); ++i) {
        Geometry& g = *meshes[i]->geometry;
        g.positions = frames[i].vertices;
        g.normals = frames[i].normals;
        g.elements.front().count = static_cast<std::uint32_t>(g.positions.size() / 3);
        if (auto it = faceUVs.find(i); it != faceUVs.end() && it->second.size() == g.positions.size() / 3 * 2) g.uvs = it->second;
        else g.uvs.assign(g.positions.size() / 3 * 2, 0);
        g.touch();
    }
    current = std::move(frames);
    placeDecor();
}

void CharacterModel::dress(const AvatarConfiguration& look) {
    avatar = look;
    auto catalog = ld::avatar_catalog::items();
    std::vector<AvatarItem> worn;
    for (const auto& e : look.equipped)
        if (auto it = catalog.find(e.itemID); it != catalog.end()) worn.push_back(it->second);
    for (std::size_t i = 0; i < std::min(meshes.size(), asset->names.size()); ++i) {
        auto part = bodyPartOfMesh(asset->names[i]);
        if (!part) continue;
        Material m;
        m.diffuse = Color::hex(look.color(*part));
        if (faceUVs.count(i))
            if (auto picture = face::texture(face::chosen(look))) {
                m.texture = picture;
                m.decal = true;
            }
        meshes[i]->material = m;
    }
    headAnchor->clear();
    backAnchor->clear();
    for (const auto& item : worn) {
        if (!item.category.accessory()) continue;
        auto accessory = avatar_catalog::accessory(item);
        if (!accessory) continue;
        const std::string& c = item.category.name;
        (c == "hat" || c == "hair" || c == "faceAccessory" ? headAnchor : backAnchor)->add(accessory);
    }
    if (!headAnchor->parent()) {
        node->add(headAnchor);
        node->add(backAnchor);
    }
    placeDecor();
}

void CharacterModel::placeDecor() {
    if (current.size() != asset->names.size()) return;
    for (std::size_t i = 0; i < asset->names.size(); ++i) {
        const std::string& n = asset->names[i];
        if (n.rfind("Head_", 0) == 0) {
            Bounds b = frameBounds(current[i]);
            headAnchor->position = b.center();
            headAnchor->scale = b.extent();
            break;
        }
    }
    for (std::size_t i = 0; i < asset->names.size(); ++i) {
        const std::string& n = asset->names[i];
        if (n.rfind("Body_", 0) == 0) {
            Bounds b = frameBounds(current[i]);
            backAnchor->position = b.center();
            backAnchor->scale = b.extent();
            break;
        }
    }
}

namespace name_tag {

NodePtr make(const std::string& text) {
    auto texture = textTexture(text, "HelveticaNeue-Bold", 96, {1, 1, 1, 1});
    double aspect = static_cast<double>(texture->width) / std::max(1, texture->height);
    auto glyphs = Node::make(shapes::plane(aspect, 1));
    Material ink;
    ink.lighting = Lighting::constant;
    ink.texture = texture;
    glyphs->geometry->material() = ink;
    glyphs->renderingOrder = 31;
    glyphs->castsShadow = false;
    auto plate = Node::make(shapes::plane(aspect + 0.9, 1.7, 0.45));
    Material backing;
    backing.lighting = Lighting::constant;
    backing.diffuse = {0.06, 0.06, 0.06, 0.55};
    plate->geometry->material() = backing;
    plate->position = {0, 0, -0.02};
    plate->renderingOrder = 30;
    plate->castsShadow = false;
    auto tag = Node::make(name);
    tag->position = {0, lift, 0};
    tag->billboard = true;
    tag->castsShadow = false;
    tag->add(plate);
    tag->add(glyphs);
    tag->scale = {low, low, low};
    return tag;
}

void fit(const std::vector<NodePtr>& bodies, Vec3 camera) {
    for (const auto& body : bodies) {
        if (!body) continue;
        auto tag = body->child(name);
        if (!tag) continue;
        Vec3 at = tag->worldPosition();
        double h = std::clamp((at - camera).length() * perUnit, low, high);
        tag->scale = {h, h, h};
    }
}

}

namespace scene_factory {

CharacterPtr avatar(const AvatarConfiguration& look, const std::optional<std::filesystem::path>& resource) {
    auto path = resource.value_or(resourceFolder() / "Characters" / (look.bodyPreset == "female" ? "Female.character" : "Male.character"));
    auto asset = CharacterAsset::load(path);
    if (!asset) return nullptr;
    return std::make_shared<CharacterModel>(asset, look);
}

void warm() {
    auto folder = resourceFolder() / "Characters";
    backgroundPool().post([folder] {
        for (const char* name : {"Male.character", "Female.character"}) CharacterAsset::load(folder / name);
    });
}

void base(Scene& scene) {
    scene.background = {BackgroundKind::color, Color::hex("E9E6F4"), {}, {}};
    auto ambient = Node::make("ambient");
    Light a;
    a.intensity = 650;
    ambient->light = a;
    scene.root->add(ambient);
    auto sun = Node::make("sun");
    Light s;
    s.type = LightType::directional;
    s.intensity = 900;
    s.castsShadow = true;
    sun->light = s;
    sun->setEuler({-pi / 3, -pi / 5, 0});
    scene.root->add(sun);
}

Background sky(const WorldLighting& lighting) {
    double night = 1 - std::min(1.0, lighting.daylight() * 2.5);
    Color dark = Color::hex("0E1424");
    Background b;
    b.kind = BackgroundKind::gradient;
    b.top = Color::hex(lighting.skyTop).blended(dark, night * 0.85);
    b.bottom = Color::hex(lighting.skyHorizon).blended(dark, night * 0.75);
    return b;
}

void illuminate(Scene& scene, const WorldLighting& lighting) {
    auto sun = scene.root->child("ludissey.sun");
    if (!sun) {
        sun = Node::make("ludissey.sun");
        Light l;
        l.type = LightType::directional;
        l.castsShadow = true;
        l.deferredShadows = true;
        l.shadowColor = {0, 0, 0, 0.45};
        sun->light = l;
        scene.root->add(sun);
    }
    auto ambient = scene.root->child("ludissey.ambient");
    if (!ambient) {
        ambient = Node::make("ludissey.ambient");
        ambient->light = Light{};
        scene.root->add(ambient);
    }
    double daylight = lighting.daylight();
    double elevation = std::max(0.12, std::min(pi - 0.12, (lighting.clockTime - 6) / 12 * pi));
    double tilt = std::sin(elevation) > 0 ? std::min(elevation, pi - elevation) : 0.12;
    double bearing = elevation < pi / 2 ? -pi / 5 : pi - pi / 5;
    sun->setEuler({-tilt, bearing, 0});
    sun->light->intensity = 1000 * lighting.brightness * std::max(0.12, daylight);
    sun->light->color = Color::hex("FFF4E0").blended(Color::hex("9FB4E0"), 1 - daylight);
    ambient->light->color = Color::hex(lighting.ambient);
    ambient->light->intensity = 420 + 260 * lighting.brightness * std::max(0.25, daylight);
    scene.background = sky(lighting);
    scene.fog.start = lighting.fogEnd * 0.35;
    scene.fog.end = lighting.fogEnd;
    scene.fog.color = Color::hex(lighting.skyHorizon).blended(Color::hex("0E1424"), (1 - std::min(1.0, daylight * 2.5)) * 0.75);
    scene.fog.exponent = 1;
}

GeometryPtr mesh(const std::filesystem::path& path) {
    static std::mutex lock;
    static std::map<std::string, GeometryPtr> cache;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (auto it = cache.find(path.string()); it != cache.end()) return it->second;
    }
    std::error_code error;
    if (!std::filesystem::exists(path, error)) return nullptr;
    std::string extension = path.extension().string();
    if (!extension.empty() && extension[0] == '.') extension.erase(0, 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (!mesh_import::supported(extension)) return nullptr;
    auto data = mesh_import::cached(path);
    if (!data || data->empty()) return nullptr;
    auto g = shapes::mesh(data->positions, data->normals, data->indices);
    std::lock_guard<std::mutex> guard(lock);
    cache[path.string()] = g;
    return g;
}

double edge(Vec3 size) { return std::min(edgeRadius, 0.25 * std::min({size.x, size.y, size.z})); }

GeometryPtr cuboid(Vec3 size) {
    static std::mutex lock;
    static std::map<std::tuple<long long, long long, long long>, GeometryPtr> cache;
    auto quantized = [](double v) { return static_cast<long long>(std::llround(std::max(0.0, v) * 1000)); };
    auto key = std::make_tuple(quantized(size.x), quantized(size.y), quantized(size.z));
    std::lock_guard<std::mutex> guard(lock);
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    if (cache.size() >= 2048) cache.clear();
    Vec3 exact{std::get<0>(key) / 1000.0, std::get<1>(key) / 1000.0, std::get<2>(key) / 1000.0};
    auto g = shapes::box(exact.x, exact.y, exact.z, edge(exact));
    cache[key] = g;
    return g;
}

bool cuboidal(const WorldBlock& block) { return block.kind != "mesh" && block.kind != "sphere" && block.kind != "cylinder" && block.kind != "wedge" && block.kind != "model"; }

Bounds bounds(const WorldBlock& block, const Node& node) {
    if (!node.geometry) return {{-block.sizeX / 2, -block.sizeY / 2, -block.sizeZ / 2}, {block.sizeX / 2, block.sizeY / 2, block.sizeZ / 2}};
    Bounds local = node.geometry->bounds();
    Vec3 s = node.scale;
    return {{local.low.x * s.x, local.low.y * s.y, local.low.z * s.z}, {local.high.x * s.x, local.high.y * s.y, local.high.z * s.z}};
}

Material material(const WorldBlock& block) {
    Material m;
    m.diffuse = Color::hex(block.color);
    if (block.material == "neon") {
        m.lighting = Lighting::constant;
        m.emission = Color::hex(block.color);
    } else if (block.material == "wood") m.surface = 1;
    else if (block.material == "metal") m.surface = 2;
    else if (block.material == "glass") m.surface = 3;
    m.opacity = 1 - std::min(0.95, std::max(0.0, block.transparency));
    if (block.material == "glass") m.opacity = std::min(m.opacity, 0.45);
    return m;
}

std::string structure(const WorldBlock& block) {
    std::string asset = block.meshAsset.value_or("");
    bool present = block.meshAsset && asset_cache::verified(*block.meshAsset);
    return block.kind + asset + (present ? "+" : "");
}

void apply(const WorldBlock& block, Node& node) {
    node.position = {block.x, block.y, block.z};
    node.setEulerDegrees(block.rotationX, block.rotationY, block.rotationZ);
    if (block.kind == "mesh" && node.geometry && node.geometry != unit("missing")) {
        double e = extent(node.geometry->bounds());
        double factor = e > 0 ? 1 / e : 1;
        node.scale = {factor * block.sizeX, factor * block.sizeY, factor * block.sizeZ};
    } else if (cuboidal(block)) {
        node.geometry = cuboid({block.sizeX, block.sizeY, block.sizeZ});
        node.scale = {1, 1, 1};
    } else node.scale = {block.sizeX, block.sizeY, block.sizeZ};
    if (node.geometry) node.material = material(block);
}

NodePtr block(const WorldBlock& value) {
    auto node = Node::make(value.id);
    node->position = {value.x, value.y, value.z};
    node->setEulerDegrees(value.rotationX, value.rotationY, value.rotationZ);
    if (value.kind == "mesh") {
        auto path = value.meshAsset ? asset_cache::verified(*value.meshAsset) : std::nullopt;
        GeometryPtr g = path ? mesh(*path) : nullptr;
        if (g) {
            node->geometry = g;
            node->material = material(value);
            double e = extent(g->bounds());
            double factor = e > 0 ? 1 / e : 1;
            node->scale = {factor * value.sizeX, factor * value.sizeY, factor * value.sizeZ};
        } else {
            node->geometry = unit("missing");
            node->material = missingAsset();
            node->scale = {value.sizeX, value.sizeY, value.sizeZ};
        }
        return node;
    }
    if (cuboidal(value)) {
        node->geometry = cuboid({value.sizeX, value.sizeY, value.sizeZ});
        node->material = material(value);
        return node;
    }
    node->geometry = unit(value.kind == "sphere" || value.kind == "cylinder" || value.kind == "wedge" ? value.kind : "cube");
    node->material = material(value);
    node->scale = {value.sizeX, value.sizeY, value.sizeZ};
    return node;
}

}

namespace avatar_catalog {

NodePtr accessory(const AvatarItem& item) {
    if (!item.asset) return nullptr;
    auto path = asset_cache::verified(*item.asset);
    if (!path) return nullptr;
    auto g = scene_factory::mesh(*path);
    if (!g) return nullptr;
    auto node = Node::make(g, "accessory." + item.id);
    Bounds b = g->bounds();
    double e = extent(b);
    if (e > 0) node->scale = {1 / e, 1 / e, 1 / e};
    node->pivot = Mat4::translation({(b.high.x + b.low.x) / 2, b.low.y, (b.high.z + b.low.z) / 2});
    const std::string& c = item.category.name;
    Vec3 offset{0, -0.5, 0};
    if (c == "hat") offset = {0, 0.45, 0};
    else if (c == "hair") offset = {0, 0.2, 0};
    else if (c == "faceAccessory") offset = {0, -0.1, 0.5};
    else if (c == "neckAccessory") offset = {0, 0.45, 0};
    else if (c == "shoulderAccessory") offset = {0.5, 0.45, 0};
    else if (c == "frontAccessory") offset = {0, 0, 0.5};
    else if (c == "backAccessory") offset = {0, -0.2, -0.5};
    node->position = offset;
    return node;
}

}

void TerrainView::sync(const Terrain& terrain) {
    std::set<ChunkKey> wanted;
    for (const auto& key : terrain.keys())
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dz = -1; dz <= 1; ++dz) {
                    ChunkKey near{key.x + dx, key.y + dy, key.z + dz};
                    if (near.x >= 0 && near.y >= 0 && near.z >= 0 && near.x < Terrain::chunksX && near.y < Terrain::chunksY && near.z < Terrain::chunksZ) wanted.insert(near);
                }
    bool changed = false;
    for (auto it = nodes.begin(); it != nodes.end();) {
        if (wanted.count(it->first)) {
            ++it;
            continue;
        }
        it->second->remove();
        signatures.erase(it->first);
        it = nodes.erase(it);
        changed = true;
    }
    for (const auto& key : wanted) {
        std::uint64_t signature = terrain.neighbourhood(key);
        if (auto it = signatures.find(key); it != signatures.end() && it->second == signature) continue;
        signatures[key] = signature;
        changed = true;
        ++rebuilt;
        if (auto it = nodes.find(key); it != nodes.end()) it->second->remove();
        auto node = Node::make("terrain." + key.text());
        TerrainMesh mesh = meshChunk(terrain, key, false);
        if (!mesh.empty()) {
            triangles += static_cast<int>(mesh.indices.size() / 3);
            auto g = std::make_shared<Geometry>();
            for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
                g->positions.insert(g->positions.end(), {static_cast<float>(mesh.positions[i].x), static_cast<float>(mesh.positions[i].y), static_cast<float>(mesh.positions[i].z)});
                g->normals.insert(g->normals.end(), {static_cast<float>(mesh.normals[i].x), static_cast<float>(mesh.normals[i].y), static_cast<float>(mesh.normals[i].z)});
                g->colors.insert(g->colors.end(), {static_cast<float>(mesh.colors[i].x), static_cast<float>(mesh.colors[i].y), static_cast<float>(mesh.colors[i].z)});
            }
            g->materials.clear();
            std::map<std::uint8_t, std::vector<std::uint32_t>> groups;
            for (std::size_t start = 0; start + 2 < mesh.indices.size(); start += 3) {
                std::uint8_t kind = mesh.kinds[mesh.indices[start]];
                auto& group = groups[kind];
                group.insert(group.end(), mesh.indices.begin() + static_cast<std::ptrdiff_t>(start), mesh.indices.begin() + static_cast<std::ptrdiff_t>(start + 3));
            }
            int index = 0;
            for (const auto& [kind, indices] : groups) {
                g->elements.push_back({static_cast<std::uint32_t>(g->indices.size()), static_cast<std::uint32_t>(indices.size()), index++});
                g->indices.insert(g->indices.end(), indices.begin(), indices.end());
                g->materials.push_back(terrainMaterial(static_cast<TerrainMaterial>(kind)));
            }
            g->finish();
            node->add(Node::make(g, "terrain.solid"));
        }
        root->add(node);
        nodes[key] = node;
    }
    std::uint64_t look = std::hash<std::string>{}(terrain.waterProperties.json().dump());
    if (changed || surfaceSignature != look) {
        surfaceSignature = look;
        if (surface) surface->remove();
        surface.reset();
        TerrainMesh mesh = meshWater(terrain);
        if (!mesh.empty()) {
            triangles += static_cast<int>(mesh.indices.size() / 3);
            auto g = std::make_shared<Geometry>();
            for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
                g->positions.insert(g->positions.end(), {static_cast<float>(mesh.positions[i].x), static_cast<float>(mesh.positions[i].y), static_cast<float>(mesh.positions[i].z)});
                g->normals.insert(g->normals.end(), {static_cast<float>(mesh.normals[i].x), static_cast<float>(mesh.normals[i].y), static_cast<float>(mesh.normals[i].z)});
                g->colors.insert(g->colors.end(), {1.0f, 1.0f, 1.0f});
            }
            g->indices = mesh.indices;
            g->materials.clear();
            g->materials.push_back(waterMaterial(terrain.waterProperties));
            g->elements.push_back({0, static_cast<std::uint32_t>(mesh.indices.size()), 0});
            if (openSea(terrain)) {
                auto start = static_cast<std::uint32_t>(g->indices.size());
                addSeaRing(*g, mesh);
                if (g->indices.size() > start) {
                    Material deep = waterMaterial(terrain.waterProperties);
                    deep.deep = true;
                    g->materials.push_back(deep);
                    g->elements.push_back({start, static_cast<std::uint32_t>(g->indices.size()) - start, 1});
                }
            }
            g->finish();
            surface = Node::make(g, "terrain.water");
            surface->renderingOrder = 10;
            surface->castsShadow = false;
            root->add(surface);
        }
    }
    current = terrain;
    water = surface != nullptr;
}

std::optional<Underwater> TerrainView::underwater(Vec3 point) const {
    if (!water || !current.water(point.x, point.y, point.z)) return std::nullopt;
    return Underwater{Color::hex(current.waterProperties.color, Color{0.24, 0.5, 0.84, 1}), current.waterProperties.transparency};
}

std::unique_ptr<WorldScene> worldScene(const Experience& experience) {
    auto world = std::make_unique<WorldScene>();
    scene_factory::illuminate(world->scene, experience.lighting);
    world->terrain.sync(experience.terrain);
    world->scene.root->add(world->terrain.root);
    world->scene.underwater = [view = &world->terrain](Vec3 p) { return view->underwater(p); };
    for (const auto& b : experience.workspaceParts()) {
        auto node = scene_factory::block(b);
        world->parts[b.id] = node;
        world->scene.root->add(node);
    }
    return world;
}

}
