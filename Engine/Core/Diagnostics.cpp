#include "Diagnostics.hpp"

#include "Engine/Core/Log.hpp"

#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstring>
#include <ctime>
#include <exception>

namespace ld::diagnostics {

namespace {

constexpr int entries = 24;
constexpr int width = 160;
constexpr int kept = 5;

char file[1024];
char header[256];
char trail[entries][width];
std::atomic<unsigned> cursor{0};
char reason[256];

void put(int descriptor, const char* text) {
    ssize_t written = ::write(descriptor, text, std::strlen(text));
    static_cast<void>(written);
}

void record(const char* cause) {
    int descriptor = ::open(file, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor < 0) return;
    put(descriptor, header);
    put(descriptor, "Causa: ");
    put(descriptor, cause);
    put(descriptor, "\n");
    if (reason[0]) {
        put(descriptor, "Dettaglio: ");
        put(descriptor, reason);
        put(descriptor, "\n");
    }
    put(descriptor, "Ultime operazioni:\n");
    unsigned last = cursor.load();
    for (int i = 0; i < entries; ++i) {
        const char* line = trail[(last + static_cast<unsigned>(i)) % entries];
        if (!line[0]) continue;
        put(descriptor, "  ");
        put(descriptor, line);
        put(descriptor, "\n");
    }
    put(descriptor, "Stack:\n");
    void* frames[64];
    int count = ::backtrace(frames, 64);
    ::backtrace_symbols_fd(frames, count, descriptor);
    ::close(descriptor);
}

const char* signalName(int number) {
    switch (number) {
    case SIGSEGV: return "SIGSEGV, accesso a memoria non valida";
    case SIGBUS: return "SIGBUS, indirizzo non valido";
    case SIGABRT: return "SIGABRT, interruzione del programma";
    case SIGILL: return "SIGILL, istruzione non valida";
    case SIGFPE: return "SIGFPE, errore aritmetico";
    }
    return "segnale";
}

void fatal(int number) {
    record(signalName(number));
    std::signal(number, SIG_DFL);
    ::raise(number);
}

void uncaught() {
    if (auto error = std::current_exception()) {
        try {
            std::rethrow_exception(error);
        } catch (const std::exception& e) {
            std::strncpy(reason, e.what(), sizeof reason - 1);
        } catch (...) {
            std::strncpy(reason, "eccezione di tipo sconosciuto", sizeof reason - 1);
        }
    }
    std::abort();
}

void remember(const LogEntry& entry) {
    static const char* levels[] = {"info", "avviso", "errore"};
    std::time_t now = std::time(nullptr);
    std::tm local{};
    ::localtime_r(&now, &local);
    char stamp[16];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", &local);
    std::string line = std::string(stamp) + " " + levels[static_cast<int>(entry.severity)] + " " + entry.source + ": " + entry.message;
    unsigned slot = cursor.fetch_add(1) % entries;
    std::strncpy(trail[slot], line.c_str(), width - 1);
    trail[slot][width - 1] = 0;
}

}

std::vector<std::filesystem::path> reports(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> found;
    std::error_code problem;
    for (const auto& entry : std::filesystem::directory_iterator(folder, problem))
        if (entry.path().extension() == ".txt" && entry.path().filename().string().rfind("Arresto ", 0) == 0) found.push_back(entry.path());
    std::sort(found.begin(), found.end());
    return found;
}

void install(const std::filesystem::path& folder, const std::string& application) {
    std::error_code problem;
    std::filesystem::create_directories(folder, problem);
    auto previous = reports(folder);
    for (std::size_t i = 0; i + kept <= previous.size(); ++i) std::filesystem::remove(previous[i], problem);
    std::time_t now = std::time(nullptr);
    std::tm local{};
    ::localtime_r(&now, &local);
    char stamp[32];
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H.%M.%S", &local);
    std::string path = (folder / ("Arresto " + std::string(stamp) + " " + std::to_string(::getpid()) + ".txt")).string();
    std::strncpy(file, path.c_str(), sizeof file - 1);
    std::string title = "Ludissey, arresto anomalo\nApplicazione: " + application + "\nAvvio: " + stamp + "\n";
    std::strncpy(header, title.c_str(), sizeof header - 1);
    void* warm[1];
    ::backtrace(warm, 1);
    Log::setSink(remember);
    for (int number : {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE}) std::signal(number, fatal);
    std::set_terminate(uncaught);
}

}
