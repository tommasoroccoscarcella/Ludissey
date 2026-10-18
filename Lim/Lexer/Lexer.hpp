#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace lim {

struct Position {
    std::uint32_t offset = 0;
    std::uint32_t line = 1;
    std::uint32_t column = 1;
    bool operator==(const Position&) const = default;
};

struct Range {
    Position start;
    Position end;
    bool contains(std::uint32_t offset) const { return offset >= start.offset && offset <= end.offset; }
};

enum class TokenKind {
    end,
    newline,
    identifier,
    integer,
    number,
    text,
    textStart,
    textMiddle,
    textEnd,
    keyword,
    symbol,
    comment,
    invalid
};

struct Token {
    TokenKind kind = TokenKind::end;
    std::string lexeme;
    std::string value;
    Range range;
    std::uint32_t line() const { return range.start.line; }
    std::uint32_t column() const { return range.start.column; }
    bool is(TokenKind k, std::string_view text) const { return kind == k && lexeme == text; }
    bool isKeyword(std::string_view text) const { return kind == TokenKind::keyword && lexeme == text; }
    bool isSymbol(std::string_view text) const { return kind == TokenKind::symbol && lexeme == text; }
};

struct LexError {
    std::string message;
    Range range;
};

struct LexResult {
    std::vector<Token> tokens;
    std::vector<LexError> errors;
};

bool isKeyword(std::string_view word);
const std::vector<std::string>& keywords();
LexResult lex(std::string_view source, bool keepComments = false);

}
