#pragma once

#include "Lim/AST/Ast.hpp"
#include "Lim/Bytecode/Bytecode.hpp"
#include "Lim/Diagnostics/Diagnostic.hpp"
#include "Lim/Semantic/Analyzer.hpp"

#include <optional>
#include <string>
#include <vector>

namespace lim {

struct CompileResult {
    std::optional<Module> module;
    std::vector<Diagnostic> diagnostics;
    Analysis analysis;
};

Module compileProgram(const Program& program, const Analysis& analysis, const std::string& name, ExecutionContext context);
CompileResult compileSource(std::string_view source, const std::string& name, const TypeRegistry& registry, const AnalyzeOptions& options);

}
