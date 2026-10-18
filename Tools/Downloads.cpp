#include "Engine/Core/Json.hpp"
#include "Engine/Core/Text.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using ld::Json;

namespace {

std::string shell(const fs::path& path) {
    std::string out = "'";
    for (char c : path.string()) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return out + "'";
}

bool run(const std::string& command) { return std::system(command.c_str()) == 0; }

std::string read(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::string environment(const char* name) {
    const char* value = std::getenv(name);
    return value ? value : "";
}

}

int main(int argc, char** argv) {
    if (argc < 6) {
        std::cerr << "Uso: LudisseyDownloadsTool <cartella app> <cartella download> <versione> <build> <macOS minimo>\n";
        return 2;
    }
    fs::path apps = argv[1], target = argv[2];
    std::string version = argv[3], build = argv[4], minimum = argv[5];
    std::string identity = environment("LUDISSEY_SIGN_IDENTITY"), profile = environment("LUDISSEY_NOTARY_PROFILE");
    if (identity.empty()) std::cerr << "ATTENZIONE: LUDISSEY_SIGN_IDENTITY non impostata. Le app hanno una firma locale: i DMG valgono solo per prove su questo Mac e macOS li blocca se scaricati da internet.\n";
    else if (profile.empty()) std::cerr << "ATTENZIONE: LUDISSEY_NOTARY_PROFILE non impostata. I DMG sono firmati ma non notarizzati.\n";
    fs::create_directories(target);
    Json versions = Json::object();
    const std::vector<std::pair<std::string, std::string>> products{{"ludissey", "Ludissey"}, {"studio", "Ludissey Studio"}};
    for (const auto& [key, name] : products) {
        fs::path app = apps / (name + ".app");
        if (!fs::exists(app)) {
            std::cerr << "Applicazione mancante: " << app.string() << "\n";
            return 1;
        }
        if (!identity.empty() && !run("/usr/bin/codesign --force --deep --options runtime --timestamp --sign " + shell(fs::path(identity)) + " " + shell(app))) return 1;
        std::string file = name == "Ludissey" ? "Ludissey.dmg" : "Ludissey-Studio.dmg";
        fs::path stage = target / ("Stage-" + key), image = target / file;
        fs::remove_all(stage);
        fs::remove(image);
        fs::create_directories(stage);
        if (!run("/bin/cp -R " + shell(app) + " " + shell(stage / (name + ".app")))) return 1;
        fs::create_directory_symlink("/Applications", stage / "Applications");
        bool made = run("/usr/bin/hdiutil create -quiet -volname " + shell(fs::path(name)) + " -srcfolder " + shell(stage) + " -format UDZO -ov " + shell(image));
        fs::remove_all(stage);
        if (!made) return 1;
        if (!identity.empty() && !run("/usr/bin/codesign --force --timestamp --sign " + shell(fs::path(identity)) + " " + shell(image))) return 1;
        if (!identity.empty() && !profile.empty()) {
            if (!run("/usr/bin/xcrun notarytool submit " + shell(image) + " --keychain-profile " + shell(fs::path(profile)) + " --wait")) return 1;
            if (!run("/usr/bin/xcrun stapler staple " + shell(image))) return 1;
        }
        std::string data = read(image);
        versions.set(key, Json::object({{"version", version}, {"build", build}, {"bytes", static_cast<double>(data.size())}, {"sha256", ld::sha256Hex(data)}, {"minimumSystem", minimum}, {"file", file}, {"signed", !identity.empty()}, {"notarized", !identity.empty() && !profile.empty()}}));
    }
    std::ofstream(target / "versions.json", std::ios::binary) << versions.dump(true) << "\n";
    return 0;
}
