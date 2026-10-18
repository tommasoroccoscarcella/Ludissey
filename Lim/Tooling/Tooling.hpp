#pragma once

#include "Lim/Diagnostics/Diagnostic.hpp"
#include "Lim/Semantic/Analyzer.hpp"
#include "Lim/Semantic/Types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lim::tooling {

enum class TokenStyle { keyword, identifier, type, function, property, number, text, comment, operatorSymbol, punctuation, invalid };

struct Span {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    TokenStyle style = TokenStyle::identifier;
    bool operator==(const Span&) const = default;
};

std::vector<Span> highlight(std::string_view source);

struct FormatResult {
    std::string text;
    bool changed = false;
    std::vector<Diagnostic> diagnostics;
};

FormatResult format(std::string_view source);

enum class CompletionKind { keyword, variable, constant, function, method, property, event, type, enumCase, module, snippet };

struct Completion {
    std::string label;
    CompletionKind kind = CompletionKind::variable;
    std::string detail;
    std::string insert;
    std::string documentation;
};

struct Context {
    const TypeRegistry& registry;
    AnalyzeOptions options;
};

std::vector<Completion> complete(std::string_view source, std::uint32_t offset, const Context& context);

struct Hover {
    Range range;
    std::string title;
    std::string documentation;
};

std::optional<Hover> hover(std::string_view source, std::uint32_t offset, const Context& context);
std::optional<Range> definition(std::string_view source, std::uint32_t offset, const Context& context);
std::vector<Range> references(std::string_view source, std::uint32_t offset, const Context& context);

struct TextEdit {
    Range range;
    std::string replacement;
};

struct RenameResult {
    std::vector<TextEdit> edits;
    std::string error;
};

RenameResult rename(std::string_view source, std::uint32_t offset, const std::string& name, const Context& context);
std::string applyEdits(std::string_view source, std::vector<TextEdit> edits);

struct Snippet {
    std::string label;
    std::string trigger;
    std::string body;
    std::string detail;
};

const std::vector<Snippet>& snippets();

}
