#include "Engine/Core/Diagnostics.hpp"
#include "Engine/Core/Log.hpp"

#include <csignal>
#include <stdexcept>
#include <string>

namespace {

[[noreturn]] void fail() { throw std::runtime_error("stato non valido"); }

void guarded() noexcept { fail(); }

}

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    ld::diagnostics::install(argv[1], argv[2]);
    std::string mode = argv[3];
    if (mode == "install") return 0;
    ld::Log::info("Studio", "apertura progetto, 3 parti, 1 istanze");
    ld::Log::info("Studio", "Play personaggio");
    if (mode == "segv") std::raise(SIGSEGV);
    if (mode == "throw") guarded();
    return 3;
}
