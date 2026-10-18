#pragma once

#include "Lim/AST/Ast.hpp"
#include "Lim/Diagnostics/Diagnostic.hpp"

#include <string_view>

namespace lim {

struct ParseResult {
    Program program;
    std::vector<Diagnostic> diagnostics;
    std::vector<Token> tokens;
};

ParseResult parse(std::string_view source, std::string file = "script");

}
