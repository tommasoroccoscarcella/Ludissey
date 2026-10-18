#include "Engine/Avatar/Avatar.hpp"
#include "Engine/Core/Json.hpp"
#include "Engine/Core/Resources.hpp"
#include "Engine/Rendering/World.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
using ld::Json;

namespace {

Json rounded(const std::vector<float>& values, double scale) {
    Json out = Json::array();
    for (float v : values) out.push(std::round(static_cast<double>(v) * scale) / scale);
    return out;
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Uso: LudisseyCharacters <radice del repository>\n";
        return 2;
    }
    fs::path root = argv[1];
    ld::setResourceFolder(root / "Resources");
    Json bodies = Json::object();
    for (const char* body : {"male", "female"}) {
        auto model = ld::render::scene_factory::avatar(ld::AvatarConfiguration::standard("", body));
        if (!model) {
            std::cerr << "Personaggio non leggibile: " << body << "\n";
            return 1;
        }
        Json parts = Json::array();
        const auto& frames = model->frames();
        for (std::size_t i = 0; i < model->parts().size() && i < frames.size(); ++i) {
            auto part = ld::bodyPartOfMesh(model->parts()[i]);
            if (!part) continue;
            Json entry = Json::object({{"part", std::string(ld::bodyPartName(*part))}, {"vertices", rounded(frames[i].vertices, 1000)}, {"normals", rounded(frames[i].normals, 100)}});
            if (auto face = model->faces().find(i); face != model->faces().end()) entry.set("face", rounded(face->second, 1000));
            parts.push(entry);
        }
        bodies.set(body, parts);
    }
    std::ofstream(root / "Web/characters.js", std::ios::binary) << "var LudisseyCharacters = " << bodies.dump() << ";\n";
    std::cout << "Personaggi esportati in Web/characters.js\n";
    return 0;
}
