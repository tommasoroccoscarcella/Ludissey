#include "Diagnostic.hpp"

#include <algorithm>

namespace lim {

std::string summary(const Diagnostic& d) {
    std::string out = d.file.empty() ? std::string("script") : d.file;
    out += ":" + std::to_string(d.range.start.line) + ":" + std::to_string(d.range.start.column) + ": ";
    out += d.severity == Severity::error ? "errore: " : d.severity == Severity::warning ? "avviso: " : "suggerimento: ";
    out += d.message;
    return out;
}

std::string render(const Diagnostic& d, std::string_view source) {
    std::string out = summary(d) + "\n";
    std::size_t start = 0;
    std::uint32_t line = 1;
    while (line < d.range.start.line && start < source.size()) {
        if (source[start] == '\n') ++line;
        ++start;
    }
    std::size_t end = source.find('\n', start);
    if (end == std::string_view::npos) end = source.size();
    std::string text(source.substr(start, end - start));
    std::string number = std::to_string(d.range.start.line);
    out += " " + number + " | " + text + "\n";
    out += std::string(number.size() + 1, ' ') + " | ";
    std::uint32_t column = 1;
    std::size_t index = 0;
    while (column < d.range.start.column && index < text.size()) {
        out += text[index] == '\t' ? '\t' : ' ';
        if ((static_cast<unsigned char>(text[index]) & 0xC0) != 0x80) ++column;
        ++index;
    }
    std::uint32_t width = d.range.end.line == d.range.start.line && d.range.end.column > d.range.start.column ? d.range.end.column - d.range.start.column : 1;
    out += std::string(std::max<std::uint32_t>(1, width), '^') + "\n";
    if (!d.expected.empty()) out += "   atteso: " + d.expected + "\n";
    if (!d.actual.empty()) out += "   trovato: " + d.actual + "\n";
    if (!d.help.empty()) out += "   aiuto: " + d.help + "\n";
    return out;
}

bool hasErrors(const std::vector<Diagnostic>& diagnostics) {
    return std::any_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) { return d.severity == Severity::error; });
}

}
