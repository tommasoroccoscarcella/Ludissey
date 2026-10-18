#import <AppKit/AppKit.h>

#include "Engine/Client/Models.hpp"
#include "Engine/Core/Png.hpp"
#include "Engine/Core/Text.hpp"
#include "Engine/Experience/Experience.hpp"
#include "Engine/Studio/StudioDocument.hpp"
#include "Golden.hpp"
#include "Interface/Components.hpp"
#include "Interface/Glyphs.hpp"
#include "Interface/Surface.hpp"
#include "Platform/Mac/Platform.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <fstream>
#include <regex>
#include <set>
#include <sstream>

namespace {

std::filesystem::path pack() { return golden::root() / "Resources/Icons/Pack"; }

std::string bytes(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::stringstream data;
    data << in.rdbuf();
    return data.str();
}

std::uint32_t big(const std::string& data, std::size_t at) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(data[at])) << 24) | (static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + 1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + 2])) << 8) | static_cast<std::uint32_t>(static_cast<unsigned char>(data[at + 3]));
}

std::set<std::string> chunks(const std::string& png) {
    std::set<std::string> kinds;
    for (std::size_t at = 8; at + 12 <= png.size();) {
        kinds.insert(png.substr(at + 4, 4));
        at += 12 + big(png, at);
    }
    return kinds;
}

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file);
    std::stringstream data;
    data << in.rdbuf();
    return data.str();
}

}

TEST_CASE("Icone: master del pacchetto dalle anteprime .af", "[icons]") {
    std::set<std::string> masters, sources;
    for (const auto& entry : std::filesystem::directory_iterator(pack()))
        if (entry.path().filename().string()[0] != '.') masters.insert(entry.path().filename().string());
    for (const auto& entry : std::filesystem::directory_iterator(golden::root() / "Design/Icons")) {
        if (entry.path().extension() != ".af") continue;
        std::string name;
        for (char c : entry.path().stem().string())
            if (c != ' ') name += c;
        name += ".png";
        sources.insert(name);
        INFO(name);
        std::string master = bytes(pack() / name), design = bytes(entry.path());
        REQUIRE(master.size() > 24);
        CHECK(design.find(master) != std::string::npos);
        CHECK(big(master, 16) == big(master, 20));
        CHECK(big(master, 16) >= 256);
        CHECK(static_cast<int>(static_cast<unsigned char>(master[25])) == 6);
        auto kinds = chunks(master);
        for (const char* text : {"tEXt", "iTXt", "zTXt"}) CHECK_FALSE(kinds.count(text));
    }
    CHECK(masters == sources);
    CHECK(masters.size() >= 66);
    CHECK_FALSE(std::filesystem::exists(golden::root() / "Resources/Icons/Ludissey Icons 48px"));
    CHECK_FALSE(std::filesystem::exists(golden::root() / "Resources/Icons/Fixed Icon Pack"));
    CHECK_FALSE(std::filesystem::exists(golden::root() / "Web/icons.zip"));
}

TEST_CASE("Icone: registro semantico e superfici", "[icons]") {
    golden::registry();
    const auto& semantic = ui::icons::semanticFiles();
    const auto& missing = ui::icons::placeholders();
    for (const auto& [name, substitute] : missing) CHECK_FALSE(semantic.count(name));
    for (const char* name : {"ld-icon-object-value", "ld-icon-object-tool", "ld-icon-object-mesh", "ld-icon-panel-output", "ld-icon-service-players"}) {
        INFO(name);
        CHECK_FALSE(semantic.count(name));
        CHECK(missing.count(name));
    }
    for (const auto& [name, file] : semantic) {
        INFO(name);
        if (name.rfind("ld-icon-app-", 0) != 0) CHECK(file != "Friends.png");
    }
    CHECK(ui::icons::placeholder("ld-icon-service-players") == "amici");
    for (const auto& [name, file] : semantic) {
        INFO(name);
        CHECK(name.rfind("ld-icon-", 0) == 0);
        CHECK(std::filesystem::exists(pack() / file));
        auto image = ui::icons::load(name);
        CHECK(image.valid());
        CHECK(image.pixelsWide >= 256);
        auto small = ui::icons::sized(name, 24);
        CHECK(small.pixelsWide == 24);
        CHECK(ui::icons::sized(name, 4096).pixelsWide == image.pixelsWide);
    }
    std::set<std::string> used;
    for (auto tool : {ld::StudioTool::select, ld::StudioTool::move, ld::StudioTool::scale, ld::StudioTool::rotate}) used.insert(std::string(ld::toolSymbol(tool)));
    const auto& registry = ld::ClassRegistry::shared();
    for (const auto& service : registry.services()) used.insert(service.icon);
    for (auto kind : ld::rules::kinds) used.insert(ld::kindSymbol(kind));
    for (const auto& [name, descriptor] : registry.descriptors())
        if (descriptor.implemented) used.insert(descriptor.icon);
    for (const auto& name : used) {
        INFO("icona usata " << name);
        CHECK((semantic.count(name) || missing.count(name)));
    }
    std::map<std::string, std::string> studio = {{"ld-icon-studio-select", "Select"}, {"ld-icon-studio-move", "Move"}, {"ld-icon-studio-scale", "Scale"}, {"ld-icon-studio-rotate", "Rotate"},
                                                 {"ld-icon-panel-explorer", "Explorer"}, {"ld-icon-panel-properties", "Properties"}, {"ld-icon-panel-toolbox", "Toolbox"}, {"ld-icon-studio-group", "Group"},
                                                 {"ld-icon-studio-ungroup", "Separate"}, {"ld-icon-action-import", "Insert Object"}, {"ld-icon-object-localscript", "Local Script"},
                                                 {"ld-icon-object-modulescript", "ModuleScript"}, {"ld-icon-object-script", "Script"}, {"ld-icon-object-folder", "Folder"}};
    for (const auto& [name, stem] : studio) {
        INFO(name);
        std::string file;
        for (char c : stem)
            if (c != ' ') file += c;
        CHECK(semantic.at(name) == file + ".png");
    }
    for (const auto& [className, name] : std::map<std::string, std::string>{{"LocalScript", "ld-icon-object-localscript"}, {"ModuleScript", "ld-icon-object-modulescript"}, {"Script", "ld-icon-object-script"},
                                                                            {"Folder", "ld-icon-object-folder"}, {"Model", "ld-icon-object-model"}, {"Part", "ld-icon-object-part"}}) {
        INFO(className);
        REQUIRE(registry.find(className));
        CHECK(registry.find(className)->icon == name);
    }
}

TEST_CASE("Glifi: sito e app dalla stessa fonte", "[icons][web]") {
    std::string module = read(golden::root() / "Web/icons.js");
    std::set<std::string> drawn;
    for (const auto& entry : std::filesystem::directory_iterator(golden::root() / "Resources/Icons/Glyphs")) drawn.insert(entry.path().stem().string());
    CHECK(drawn.size() == 11);
    std::set<std::string> native;
    for (const auto& entry : ui::glyphs::all) {
        native.insert(std::string(entry.name));
        CHECK(entry.count > 2);
        CHECK(entry.steps[0].kind == 'M');
        for (int i = 0; i < entry.count; ++i) {
            CHECK(entry.steps[i].x >= 0);
            CHECK(entry.steps[i].x <= 24);
            CHECK(entry.steps[i].y >= 0);
            CHECK(entry.steps[i].y <= 24);
        }
    }
    CHECK(native == drawn);
    for (const auto& name : drawn) {
        INFO(name);
        std::string source = read(golden::root() / "Resources/Icons/Glyphs" / (name + ".svg"));
        CHECK(module.find(ld::escapeJson(name) + ": " + ld::escapeJson(source.substr(0, source.size() - 1))) != std::string::npos);
        CHECK(source.find("<path") == source.rfind("<path"));
        CHECK(source.find("transform") == std::string::npos);
        CHECK(source.find("stroke") == std::string::npos);
    }
    std::regex studioIcon("ld-icon-(?!app-|status-)[a-z]+(?:-[a-z0-9]+)+");
    for (const char* file : {"Web/app.js", "Web/index.html", "Interface/ClientView.cpp", "Player/PlayerView.cpp"}) {
        INFO(file);
        std::string source = read(golden::root() / file);
        CHECK_FALSE(std::regex_search(source, studioIcon));
    }
    for (const auto& [name, file] : ui::icons::semanticFiles())
        if (name.rfind("ld-icon-app-", 0) == 0) CHECK(module.find(ld::escapeJson(name)) != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(golden::root() / "Web/icons"));
}

TEST_CASE("Glifi: disegnati nel colore richiesto", "[icons][rendering]") {
    for (const auto& entry : ui::glyphs::all) {
        std::string name(entry.name);
        INFO(name);
        ui::Surface surface([&] { return ui::glyph(name, 24, ui::theme::colors::header)->padding(4)->background(ui::rectangle(ui::Color::white())); });
        surface.resize({32, 32}, 2);
        ui::Canvas canvas(32, 32, 2);
        surface.render(canvas);
        int inked = 0, tinted = 0, total = canvas.pixelWidth() * canvas.pixelHeight();
        const std::uint8_t* p = canvas.pixels();
        for (int i = 0; i < total; ++i) {
            if (p[i * 4] + p[i * 4 + 1] + p[i * 4 + 2] < 0.9 * 3 * 255) ++inked;
            if (std::abs(p[i * 4] / 255.0 - ui::theme::colors::header.r) < 0.02 && std::abs(p[i * 4 + 2] / 255.0 - ui::theme::colors::header.b) < 0.02) ++tinted;
        }
        CHECK(inked > total / 40);
        CHECK(tinted > inked / 2);
    }
}

TEST_CASE("Icone: disegnate entro 32 pt e non vuote", "[icons][rendering]") {
    @autoreleasepool {
        [NSApplication sharedApplication];
        platform::installAdapters();
        for (auto [name, size] : std::vector<std::pair<std::string, double>>{{"ld-icon-studio-move", ui::theme::iconSize::ribbon}, {"ld-icon-panel-explorer", ui::theme::iconSize::panel},
                                                                             {"ld-icon-service-players", ui::theme::iconSize::toolbar},
                                                                             {"ld-icon-object-part", 64}}) {
            INFO(name);
            std::string iconName = name;
            double iconSize = size;
            ui::Surface surface([&] { return ui::icon(iconName, iconSize)->padding(4)->background(ui::rectangle(ui::Color::white())); });
            double side = std::min(size, ui::theme::iconSize::ribbon) + 8;
            surface.resize({side, side}, 2);
            ui::Canvas canvas(static_cast<int>(side), static_cast<int>(side), 2);
            surface.render(canvas);
            int inked = 0, total = canvas.pixelWidth() * canvas.pixelHeight();
            const std::uint8_t* p = canvas.pixels();
            for (int i = 0; i < total; ++i)
                if (p[i * 4] + p[i * 4 + 1] + p[i * 4 + 2] < 0.9 * 3 * 255) ++inked;
            CHECK(inked > total / 14);
        }
    }
}
