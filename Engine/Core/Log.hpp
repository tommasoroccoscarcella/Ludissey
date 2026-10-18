#pragma once

#include <functional>
#include <string>

namespace ld {

enum class Severity { info, warning, error };

struct LogEntry {
    Severity severity = Severity::info;
    std::string source;
    std::string message;
};

class Log {
public:
    using Sink = std::function<void(const LogEntry&)>;
    static void setSink(Sink sink);
    static void write(Severity severity, std::string source, std::string message);
    static void info(std::string source, std::string message) { write(Severity::info, std::move(source), std::move(message)); }
    static void warning(std::string source, std::string message) { write(Severity::warning, std::move(source), std::move(message)); }
    static void error(std::string source, std::string message) { write(Severity::error, std::move(source), std::move(message)); }
};

}
