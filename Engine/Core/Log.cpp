#include "Log.hpp"

#include <mutex>

namespace ld {

namespace {
std::mutex& guard() { static std::mutex mutex; return mutex; }
Log::Sink& sink() { static Log::Sink current; return current; }
}

void Log::setSink(Sink next) {
    std::lock_guard lock(guard());
    sink() = std::move(next);
}

void Log::write(Severity severity, std::string source, std::string message) {
    Sink current;
    {
        std::lock_guard lock(guard());
        current = sink();
    }
    if (current) current(LogEntry{severity, std::move(source), std::move(message)});
}

}
