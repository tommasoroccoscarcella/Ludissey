#include "Hygiene.hpp"

#include "Engine/Core/Text.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <cstdlib>
#include <fstream>

namespace fs = std::filesystem;

namespace {

fs::path repository() {
    const char* value = std::getenv("LUDISSEY_ROOT");
    REQUIRE(value);
    return value;
}

struct Tree {
    fs::path root = fs::temp_directory_path() / ("Ludissey Igiene " + ld::makeUuid());

    Tree() {
        write("CMakeLists.txt", "project(Prova)\n");
        write("Engine/Core/Core.cpp", "#include \"Core.hpp\"\n\nint value() { return 1; }\n");
        write("Engine/Core/Core.hpp", "#pragma once\n\nint value();\n");
    }
    ~Tree() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }

    fs::path write(const std::string& name, const std::string& content) {
        auto path = root / name;
        fs::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary) << content;
        return path;
    }

    std::vector<std::string> problems() const {
        std::vector<std::string> out;
        for (const auto& finding : hygiene::verify(root)) out.push_back(finding.kind + " " + finding.text);
        return out;
    }

    bool flags(const std::string& expected) const {
        auto all = problems();
        return std::any_of(all.begin(), all.end(), [&](const std::string& p) { return p.find(expected) != std::string::npos; });
    }
};

void flagged(const std::string& name, const std::string& content, const std::string& expected) {
    Tree tree;
    REQUIRE(tree.problems().empty());
    tree.write(name, content);
    INFO(name << " → " << expected);
    for (const auto& p : tree.problems()) UNSCOPED_INFO(p);
    CHECK(tree.flags(expected));
}

void clean(const std::string& name, const std::string& content) {
    Tree tree;
    tree.write(name, content);
    INFO(name);
    auto problems = tree.problems();
    for (const auto& p : problems) UNSCOPED_INFO(p);
    CHECK(problems.empty());
}

}

TEST_CASE("Igiene: repository conforme", "[hygiene]") {
    hygiene::sweep(repository());
    std::vector<std::string> problems;
    for (const auto& finding : hygiene::verify(repository())) problems.push_back(finding.kind + " " + finding.text);
    for (std::size_t i = 0; i < std::min<std::size_t>(problems.size(), 80); ++i) UNSCOPED_INFO(problems[i]);
    CHECK(problems.empty());
}

TEST_CASE("Igiene: riconoscimento dei commenti", "[hygiene]") {
    flagged("Engine/A.cpp", "int a = 1; // nota\n", "commento non ammesso");
    flagged("Engine/A.hpp", "#pragma once\n/* blocco */\n", "commento non ammesso");
    flagged("Platform/Mac/A.mm", "int a; /* x */\n", "commento non ammesso");
    flagged("Engine/Shader.cpp", "const char* s = R\"METAL(\nfloat4 f(); // ombra\n)METAL\";\n", "commento non ammesso");
    flagged("Web/app.js", "const a = 1; // nota\n", "commento non ammesso");
    flagged("Web/index.html", "<p>a</p>\n<!-- nota -->\n", "commento non ammesso");
    flagged("Web/style.css", "a { color: red; } /* nota */\n", "commento non ammesso");
    flagged("CMakeLists.txt", "project(Prova)\n# nota\n", "commento non ammesso");
    flagged("Web/index.html", "<script>\nlet a = 1; // nota\n</script>\n", "commento non ammesso");
    clean("Engine/B.cpp", "const char* url = \"https://esempio.test/x\";\nconst char* raw = R\"JSON({\"a\":\"//b\"})JSON\";\nchar c = '/';\n");
    clean("Web/b.js", "const re = /a\\/\\/b/g;\nconst s = 'http://x' + `//${1}`;\nconst d = 4 / 2 / 1;\n");
    clean("Web/b.css", "a { background: url(\"//cdn/x.png\"); }\n");
}

TEST_CASE("Igiene: marcatori, spazi e include", "[hygiene]") {
    flagged("Engine/C.cpp", "int TODO_value = 0;\nint x = 0; \n", "spazi finali");
    flagged("Engine/D.cpp", "int a = 0;\nbool HACK = true;\n", "marcatore vietato HACK");
    flagged("Engine/E.cpp", "int a = 0;", "newline finale");
    flagged("Engine/F.cpp", "int a = 0;\n\n", "newline finali multiple");
    flagged("Engine/G.cpp", "\xEF\xBB\xBFint a = 0;\n", "BOM");
    flagged("Engine/H.cpp", "int a = 0;\r\n", "fine riga non LF");
    flagged("Engine/I.cpp", "int f() {\n\treturn 0;\n}\n", "tabulazione");
    flagged("Engine/J.cpp", "int a;\n\n\nint b;\n", "righe vuote consecutive");
    flagged("Engine/K.cpp", "#include <vector>\n#include <map>\n\nint a;\n", "include non ordinati");
    flagged("Engine/L.cpp", "#include <map>\n#include <map>\n\nint a;\n", "include duplicato");
    flagged("Engine/M.cpp", "#include \"A.hpp\"\n#include <map>\n\nint a;\n", "tipo diverso");
    clean("Engine/N.cpp", "#include \"A.hpp\"\n\n#include <map>\n#include <vector>\n\nconst char* s = \"TODO\";\n");
}

TEST_CASE("Igiene: linguaggi ammessi", "[hygiene][languages]") {
    flagged("Engine/Vecchio.swift", "let a = 1\n", "estensione non ammessa");
    flagged("Engine/Script.lua", "print(1)\n", "estensione non ammessa");
    flagged("Tests/Fixture.lua", "print(1)\n", "estensione non ammessa");
    flagged("Tools/build.py", "print(1)\n", "estensione non ammessa");
    flagged("Engine/Tool.java", "class A {}\n", "estensione non ammessa");
    flagged("Web/app.ts", "let a = 1;\n", "estensione non ammessa");
    flagged("Engine/Bridge.mm", "int a;\n", "Objective-C++ fuori da Platform/Mac");
    flagged("Engine/Scene.cpp", "#include <SceneKit/SceneKit.h>\n", "tecnologia non ammessa");
    flagged("Engine/Lua.cpp", "int a = lua_pcall(0);\n", "tecnologia non ammessa");
    flagged("Engine/Web.cpp", "#include <JavaScriptCore/JavaScriptCore.h>\n", "tecnologia non ammessa");
    flagged("Engine/Keys.cpp", "int a = SecItemCopyMatching(0, 0);\n", "Portachiavi");
    flagged("Engine/Log.cpp", "void f() { std::cout << 1; }\n", "riferimento non ammesso");
    flagged("Engine/Path.cpp", "const char* p = \"/Users/qualcuno\";\n", "riferimento non ammesso");
    flagged("Apps/Ludissey.app/Contents/Resources/main.py", "print(1)\n", "file non previsto nel bundle");
    flagged("Apps/Ludissey.app/Contents/Resources/Note.md", "x\n", "file non previsto nel bundle");
    flagged("Apps/Helper.app/Contents/Info.plist", "x\n", "applicazione non prevista");
    clean("Apps/Ludissey.app/Contents/Resources/Web/app.js", "let a = 1;\n");
    clean("Apps/Ludissey.app/Contents/Resources/Licenses/metal-cpp.txt", "Apache License\n");
    flagged("Apps/Ludissey.app/Contents/Resources/Note.txt", "x\n", "file non previsto nel bundle");
    clean("Tests/Emulator/Emulator.js", "var a = 1;\n");
    clean("Platform/Mac/Window.mm", "int a;\n");
}

TEST_CASE("Igiene: documenti, residui e struttura", "[hygiene]") {
    flagged("Documentation/01 Architettura.md", "# Architettura\n", "documento non ammesso");
    flagged("Engine/README.md", "x\n", "documento non ammesso");
    flagged("Engine/README", "x\n", "documento non ammesso");
    flagged("Tools/Note.md", "x\n", "documento non ammesso");
    flagged("Engine/.DS_Store", "x", "file residuo");
    flagged("Engine/A.cpp.orig", "x\n", "file temporaneo");
    flagged("Engine/build/A.cpp", "int a;\n", "residuo di build");
    flagged("Tests/Ludissey-2026.ips", "{}\n", "crash dump");
    flagged("Tests/run.log", "x\n", "crash dump");
    flagged("Engine/Copia.cpp", "#include \"Core.hpp\"\n\nint value() { return 1; }\n", "file duplicato");
    flagged("Note.txt", "x\n", "file sciolto nella radice");
    flagged("Misc/A.cpp", "int a;\n", "cartella di primo livello non prevista");
    flagged("Engine/vecchio/A.cpp", "int a;\n", "PascalCase");
    Tree tree;
    fs::create_directories(tree.root / "Engine/Vuota");
    CHECK(tree.flags("cartella vuota"));
    clean("ThirdParty/Catch2/LICENSE.txt", "Licenza // con commenti\n");
    clean("ThirdParty/Catch2/README.md", "# Terze parti\n");
}
