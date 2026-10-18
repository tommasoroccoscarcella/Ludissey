#pragma once

#include "Engine/Core/Math.hpp"
#include "Engine/Core/Result.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ld {

struct MeshData {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::uint32_t> indices;
    Bounds bounds() const;
    bool empty() const { return indices.empty(); }
    void computeNormals();
};

namespace mesh_import {
bool supported(std::string_view extension);
Result<MeshData> parse(std::string_view data, std::string_view extension);
Result<MeshData> load(const std::filesystem::path& path);
std::shared_ptr<const MeshData> cached(const std::filesystem::path& path);
void setPlatformImporter(std::function<Result<MeshData>(const std::filesystem::path&)> importer);
}

}
