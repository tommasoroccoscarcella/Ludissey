#include "Mesh.hpp"

#include "Engine/Core/Json.hpp"
#include "Engine/Core/Text.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>

namespace ld {

Bounds MeshData::bounds() const {
    if (positions.empty()) return {};
    double big = std::numeric_limits<double>::max();
    Bounds b{{big, big, big}, {-big, -big, -big}};
    for (const auto& p : positions) {
        b.low = {std::min(b.low.x, p.x), std::min(b.low.y, p.y), std::min(b.low.z, p.z)};
        b.high = {std::max(b.high.x, p.x), std::max(b.high.y, p.y), std::max(b.high.z, p.z)};
    }
    return b;
}

void MeshData::computeNormals() {
    normals.assign(positions.size(), Vec3{});
    for (std::size_t i = 0; i + 2 < indices.size(); i += 3) {
        const Vec3& a = positions[indices[i]];
        const Vec3& b = positions[indices[i + 1]];
        const Vec3& c = positions[indices[i + 2]];
        Vec3 n = cross(b - a, c - a);
        for (int k = 0; k < 3; ++k) normals[indices[i + static_cast<std::size_t>(k)]] = normals[indices[i + static_cast<std::size_t>(k)]] + n;
    }
    for (auto& n : normals) n = n.normalized();
}

namespace mesh_import {

namespace {

std::function<Result<MeshData>(const std::filesystem::path&)>& platform() {
    static std::function<Result<MeshData>(const std::filesystem::path&)> importer;
    return importer;
}

Error invalid(const std::string& message) { return Error::make(ErrorCode::corrupted, message); }

std::optional<double> number(std::string_view text) { return parseNumber(trim(text)); }

Result<MeshData> obj(std::string_view data) {
    MeshData mesh;
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::map<std::pair<long long, long long>, std::uint32_t> vertices;
    bool hasNormals = true;
    for (const auto& raw : split(data, '\n')) {
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string tag;
        in >> tag;
        if (tag == "v") {
            double x = 0, y = 0, z = 0;
            if (!(in >> x >> y >> z)) return invalid("Vertice OBJ non valido");
            positions.push_back({x, y, z});
        } else if (tag == "vn") {
            double x = 0, y = 0, z = 0;
            if (!(in >> x >> y >> z)) return invalid("Normale OBJ non valida");
            normals.push_back({x, y, z});
        } else if (tag == "f") {
            std::vector<std::uint32_t> face;
            std::string item;
            while (in >> item) {
                auto parts = split(item, '/');
                auto index = number(parts[0]);
                if (!index) return invalid("Faccia OBJ non valida");
                long long p = static_cast<long long>(*index);
                p = p < 0 ? static_cast<long long>(positions.size()) + p : p - 1;
                if (p < 0 || p >= static_cast<long long>(positions.size())) return invalid("Indice OBJ fuori intervallo");
                long long n = -1;
                if (parts.size() > 2 && !parts[2].empty()) {
                    auto ni = number(parts[2]);
                    if (!ni) return invalid("Normale della faccia OBJ non valida");
                    n = static_cast<long long>(*ni);
                    n = n < 0 ? static_cast<long long>(normals.size()) + n : n - 1;
                    if (n < 0 || n >= static_cast<long long>(normals.size())) return invalid("Indice di normale OBJ fuori intervallo");
                } else hasNormals = false;
                auto key = std::make_pair(p, n);
                auto found = vertices.find(key);
                if (found == vertices.end()) {
                    std::uint32_t id = static_cast<std::uint32_t>(mesh.positions.size());
                    mesh.positions.push_back(positions[static_cast<std::size_t>(p)]);
                    mesh.normals.push_back(n >= 0 ? normals[static_cast<std::size_t>(n)] : Vec3{});
                    vertices[key] = id;
                    face.push_back(id);
                } else face.push_back(found->second);
            }
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                mesh.indices.push_back(face[0]);
                mesh.indices.push_back(face[i]);
                mesh.indices.push_back(face[i + 1]);
            }
        }
    }
    if (mesh.indices.empty()) return invalid("Nessuna mesh trovata");
    if (!hasNormals) mesh.computeNormals();
    return mesh;
}

Result<MeshData> stl(std::string_view data) {
    MeshData mesh;
    bool ascii = data.size() >= 5 && data.substr(0, 5) == "solid" && data.find("facet") != std::string_view::npos;
    if (ascii) {
        for (const auto& raw : split(data, '\n')) {
            std::string line = trim(raw);
            if (line.rfind("vertex", 0) != 0) continue;
            std::istringstream in(line.substr(6));
            double x = 0, y = 0, z = 0;
            if (!(in >> x >> y >> z)) return invalid("Vertice STL non valido");
            mesh.indices.push_back(static_cast<std::uint32_t>(mesh.positions.size()));
            mesh.positions.push_back({x, y, z});
        }
    } else {
        if (data.size() < 84) return invalid("STL troppo corto");
        std::uint32_t count = 0;
        std::memcpy(&count, data.data() + 80, 4);
        if (data.size() < 84 + static_cast<std::size_t>(count) * 50) return invalid("STL troncato");
        for (std::uint32_t t = 0; t < count; ++t) {
            const char* base = data.data() + 84 + static_cast<std::size_t>(t) * 50 + 12;
            for (int v = 0; v < 3; ++v) {
                float xyz[3];
                std::memcpy(xyz, base + v * 12, 12);
                mesh.indices.push_back(static_cast<std::uint32_t>(mesh.positions.size()));
                mesh.positions.push_back({xyz[0], xyz[1], xyz[2]});
            }
        }
    }
    if (mesh.indices.size() < 3 || mesh.indices.size() % 3) return invalid("Nessuna mesh trovata");
    mesh.computeNormals();
    return mesh;
}

Result<MeshData> ply(std::string_view data) {
    auto end = data.find("end_header");
    if (data.substr(0, 3) != "ply" || end == std::string_view::npos) return invalid("PLY non valido");
    std::string header(data.substr(0, end));
    std::size_t bodyStart = data.find('\n', end);
    if (bodyStart == std::string_view::npos) return invalid("PLY non valido");
    ++bodyStart;
    bool binary = header.find("format binary_little_endian") != std::string::npos;
    if (!binary && header.find("format ascii") == std::string::npos) return invalid("Formato PLY non supportato");
    std::size_t vertices = 0, faces = 0;
    std::vector<std::string> vertexTypes;
    std::string element;
    std::string countType = "uchar", indexType = "int";
    for (const auto& raw : split(header, '\n')) {
        std::istringstream in(trim(raw));
        std::string word;
        in >> word;
        if (word == "element") {
            std::size_t count = 0;
            in >> element >> count;
            if (element == "vertex") vertices = count;
            if (element == "face") faces = count;
        } else if (word == "property") {
            std::string type;
            in >> type;
            if (element == "vertex") vertexTypes.push_back(type);
            if (element == "face" && type == "list") in >> countType >> indexType;
        }
    }
    MeshData mesh;
    auto size = [](const std::string& t) -> std::size_t {
        if (t == "char" || t == "uchar" || t == "int8" || t == "uint8") return 1;
        if (t == "short" || t == "ushort" || t == "int16" || t == "uint16") return 2;
        if (t == "double" || t == "float64") return 8;
        return 4;
    };
    auto read = [](const char* at, const std::string& t) -> double {
        if (t == "char" || t == "int8") return static_cast<double>(*reinterpret_cast<const std::int8_t*>(at));
        if (t == "uchar" || t == "uint8") return static_cast<double>(*reinterpret_cast<const std::uint8_t*>(at));
        std::int16_t s16;
        std::uint16_t u16;
        std::int32_t s32;
        std::uint32_t u32;
        float f;
        double d;
        if (t == "short" || t == "int16") return std::memcpy(&s16, at, 2), static_cast<double>(s16);
        if (t == "ushort" || t == "uint16") return std::memcpy(&u16, at, 2), static_cast<double>(u16);
        if (t == "int" || t == "int32") return std::memcpy(&s32, at, 4), static_cast<double>(s32);
        if (t == "uint" || t == "uint32") return std::memcpy(&u32, at, 4), static_cast<double>(u32);
        if (t == "double" || t == "float64") return std::memcpy(&d, at, 8), d;
        return std::memcpy(&f, at, 4), static_cast<double>(f);
    };
    if (vertexTypes.size() < 3) return invalid("PLY senza coordinate");
    if (binary) {
        std::size_t at = bodyStart;
        for (std::size_t v = 0; v < vertices; ++v) {
            double values[3] = {0, 0, 0};
            for (std::size_t p = 0; p < vertexTypes.size(); ++p) {
                if (at + size(vertexTypes[p]) > data.size()) return invalid("PLY troncato");
                if (p < 3) values[p] = read(data.data() + at, vertexTypes[p]);
                at += size(vertexTypes[p]);
            }
            mesh.positions.push_back({values[0], values[1], values[2]});
        }
        for (std::size_t f = 0; f < faces; ++f) {
            if (at + size(countType) > data.size()) return invalid("PLY troncato");
            std::size_t n = static_cast<std::size_t>(read(data.data() + at, countType));
            at += size(countType);
            std::vector<std::uint32_t> face;
            for (std::size_t k = 0; k < n; ++k) {
                if (at + size(indexType) > data.size()) return invalid("PLY troncato");
                face.push_back(static_cast<std::uint32_t>(read(data.data() + at, indexType)));
                at += size(indexType);
            }
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                mesh.indices.push_back(face[0]);
                mesh.indices.push_back(face[i]);
                mesh.indices.push_back(face[i + 1]);
            }
        }
    } else {
        std::istringstream in{std::string(data.substr(bodyStart))};
        for (std::size_t v = 0; v < vertices; ++v) {
            std::vector<double> values(vertexTypes.size());
            for (auto& value : values)
                if (!(in >> value)) return invalid("PLY troncato");
            mesh.positions.push_back({values[0], values[1], values[2]});
        }
        for (std::size_t f = 0; f < faces; ++f) {
            std::size_t n = 0;
            if (!(in >> n)) return invalid("PLY troncato");
            std::vector<std::uint32_t> face(n);
            for (auto& index : face)
                if (!(in >> index)) return invalid("PLY troncato");
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                mesh.indices.push_back(face[0]);
                mesh.indices.push_back(face[i]);
                mesh.indices.push_back(face[i + 1]);
            }
        }
    }
    for (auto i : mesh.indices)
        if (i >= mesh.positions.size()) return invalid("Indice PLY fuori intervallo");
    if (mesh.indices.empty()) return invalid("Nessuna mesh trovata");
    mesh.computeNormals();
    return mesh;
}

}

bool supported(std::string_view extension) {
    std::string e = lowercase(extension);
    if (e == "obj" || e == "stl" || e == "ply") return true;
    return platform() && (e == "usdz" || e == "usd" || e == "usda" || e == "usdc" || e == "abc");
}

Result<MeshData> parse(std::string_view data, std::string_view extension) {
    std::string e = lowercase(extension);
    if (e == "obj") return obj(data);
    if (e == "stl") return stl(data);
    if (e == "ply") return ply(data);
    return invalid("Formato non supportato: ." + e);
}

Result<MeshData> load(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    if (!extension.empty() && extension[0] == '.') extension = extension.substr(1);
    std::string e = lowercase(extension);
    if (e != "obj" && e != "stl" && e != "ply") {
        if (platform() && supported(e)) return platform()(path);
        return invalid("Formato non supportato: ." + e);
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return Error::make(ErrorCode::notFound, "File non leggibile");
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return parse(buffer.str(), e);
}

std::shared_ptr<const MeshData> cached(const std::filesystem::path& path) {
    static std::mutex lock;
    static std::map<std::string, std::shared_ptr<const MeshData>> cache;
    std::lock_guard<std::mutex> guard(lock);
    auto key = path.string();
    if (auto it = cache.find(key); it != cache.end()) return it->second;
    auto loaded = load(path);
    if (!loaded) return nullptr;
    auto shared = std::make_shared<const MeshData>(std::move(*loaded));
    cache[key] = shared;
    return shared;
}

void setPlatformImporter(std::function<Result<MeshData>(const std::filesystem::path&)> importer) { platform() = std::move(importer); }

}

}
