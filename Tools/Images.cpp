#include "Engine/Core/Png.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

bool clean(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string data{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    input.close();
    if (data.size() < 8 || data.compare(0, 8, "\x89PNG\r\n\x1a\n") != 0) return false;
    std::string output = ld::stripPng(data);
    if (output != data) std::ofstream(path, std::ios::binary | std::ios::trunc) << output;
    return true;
}

}

int main(int argc, char** argv) {
    if (argc < 3 || std::string(argv[1]) != "clean") {
        std::cerr << "Uso: LudisseyImages clean <immagine.png>...\n";
        return 2;
    }
    for (int i = 2; i < argc; ++i)
        if (!clean(argv[i])) {
            std::cerr << "PNG non valido: " << argv[i] << "\n";
            return 1;
        }
    return 0;
}
