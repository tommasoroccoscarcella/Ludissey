#include "Engine/Core/Diagnostics.hpp"
#include "Engine/Core/Log.hpp"
#include "Engine/Core/Text.hpp"
#include "Golden.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <crt_externs.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fstream>
#include <vector>

using namespace ld;

namespace {

int probe(const std::filesystem::path& folder, const std::string& application, const std::string& mode) {
    std::string program = LUDISSEY_CRASH_PROBE, location = folder.string();
    std::vector<char*> arguments{program.data(), location.data(), const_cast<char*>(application.c_str()), const_cast<char*>(mode.c_str()), nullptr};
    pid_t child = 0;
    REQUIRE(posix_spawn(&child, program.c_str(), nullptr, nullptr, arguments.data(), *_NSGetEnviron()) == 0);
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    return status;
}

std::string crashed(const std::filesystem::path& folder, const std::string& mode) {
    int status = probe(folder, "Ludissey Studio", mode);
    CHECK(WIFSIGNALED(status));
    auto found = diagnostics::reports(folder);
    REQUIRE(found.size() == 1);
    std::string text = golden::readText(found.front());
    std::filesystem::remove(found.front());
    return text;
}

}

TEST_CASE("Diagnostica: arresto anomalo senza dati sensibili", "[diagnostics]") {
    auto folder = std::filesystem::temp_directory_path() / ("Ludissey Diagnostica " + makeUuid());
    std::string segfault = crashed(folder, "segv");
    INFO(segfault);
    CHECK(segfault.find("Applicazione: Ludissey Studio") != std::string::npos);
    CHECK(segfault.find("Causa: SIGSEGV") != std::string::npos);
    CHECK(segfault.find("Studio: apertura progetto, 3 parti, 1 istanze") != std::string::npos);
    CHECK(segfault.find("Studio: Play personaggio") != std::string::npos);
    CHECK(segfault.find("Stack:") != std::string::npos);
    CHECK(segfault.find("LudisseyCrashProbe") != std::string::npos);
    std::string thrown = crashed(folder, "throw");
    INFO(thrown);
    CHECK(thrown.find("Causa: SIGABRT") != std::string::npos);
    CHECK(thrown.find("Dettaglio: stato non valido") != std::string::npos);
    for (const std::string& text : {segfault, thrown})
        for (const char* forbidden : {"ticket", "password", "token", "secret", "Authorization"}) CHECK(lowercase(text).find(lowercase(forbidden)) == std::string::npos);
    std::filesystem::create_directories(folder);
    for (int i = 0; i < 9; ++i) std::ofstream(folder / ("Arresto 2026-01-0" + std::to_string(i) + " 00.00.00 1.txt")) << "vecchio\n";
    int status = probe(folder, "Ludissey", "install");
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
    CHECK(diagnostics::reports(folder).size() == 4);
    std::filesystem::remove_all(folder);
}
