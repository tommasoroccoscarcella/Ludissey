#pragma once

#include "Lim/Lexer/Lexer.hpp"

#include <string>
#include <vector>

namespace lim {

enum class Severity { error, warning, hint };

struct Diagnostic {
    Severity severity = Severity::error;
    std::string code;
    std::string message;
    std::string file;
    Range range;
    std::string expected;
    std::string actual;
    std::string help;
};

std::string render(const Diagnostic& diagnostic, std::string_view source);
std::string summary(const Diagnostic& diagnostic);
bool hasErrors(const std::vector<Diagnostic>& diagnostics);

}
