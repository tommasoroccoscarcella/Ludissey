#include "Lexer.hpp"

#include "Localization/Localization.hpp"

#include <algorithm>
#include <cctype>

namespace lim {

using ld::tr;

namespace {

const std::vector<std::string> words = {"let", "var", "fn", "return", "if", "else", "while", "for", "in", "break", "continue", "match", "class", "enum",
                                        "import", "async", "await", "on", "true", "false", "none", "self", "is", "as", "and", "or", "not", "export"};

const std::vector<std::string> symbols = {"...", "..<", "?.", "??", "==", "!=", "<=", ">=", "&&", "||", "->", "+=", "-=", "*=", "/=", "%=",
                                          "+", "-", "*", "/", "%", "=", "<", ">", "!", "(", ")", "[", "]", "{", "}", ",", ".", ":", "?", ";", "@"};

bool identifierStart(unsigned char c) { return std::isalpha(c) || c == '_' || c >= 0x80; }
bool identifierPart(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

void appendUtf8(std::string& out, std::uint32_t code) {
    if (code < 0x80) out += static_cast<char>(code);
    else if (code < 0x800) { out += static_cast<char>(0xC0 | (code >> 6)); out += static_cast<char>(0x80 | (code & 0x3F)); }
    else if (code < 0x10000) { out += static_cast<char>(0xE0 | (code >> 12)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
    else { out += static_cast<char>(0xF0 | (code >> 18)); out += static_cast<char>(0x80 | ((code >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((code >> 6) & 0x3F)); out += static_cast<char>(0x80 | (code & 0x3F)); }
}

class Lexer {
public:
    Lexer(std::string_view source, bool keepComments) : source(source), keep(keepComments) {}

    LexResult run() {
        while (true) {
            if (!interpolations.empty() && interpolations.back() == 0 && resumeString) {
                resumeString = false;
                interpolations.pop_back();
                string(false);
                continue;
            }
            skipSpaces();
            if (atEnd()) break;
            Position start = here();
            unsigned char c = static_cast<unsigned char>(peek());
            if (c == '\n' || c == '\r') {
                advance();
                if (c == '\r' && peek() == '\n') advance();
                if (result.tokens.empty() || result.tokens.back().kind != TokenKind::newline) push(TokenKind::newline, "\n", "", start);
                continue;
            }
            if (c == '/' && peek(1) == '/') { lineComment(start); continue; }
            if (c == '/' && peek(1) == '*') { blockComment(start); continue; }
            if (identifierStart(c)) { identifier(start); continue; }
            if (std::isdigit(c)) { number(start); continue; }
            if (c == '"') { advance(); string(true, start); continue; }
            if (!interpolations.empty()) {
                if (c == '(') ++interpolations.back();
                if (c == ')') {
                    if (interpolations.back() == 0) {
                        advance();
                        resumeString = true;
                        continue;
                    }
                    --interpolations.back();
                }
            }
            if (!symbol(start)) {
                advance();
                error(tr("Carattere non valido"), start);
                push(TokenKind::invalid, std::string(1, static_cast<char>(c)), "", start);
            }
        }
        if (!interpolations.empty()) error(tr("Interpolazione non terminata"), here());
        push(TokenKind::end, "", "", here());
        return std::move(result);
    }

private:
    std::string_view source;
    bool keep;
    std::size_t index = 0;
    std::uint32_t line = 1, column = 1;
    LexResult result;
    std::vector<int> interpolations;
    bool resumeString = false;
    Position stringOrigin;

    bool atEnd() const { return index >= source.size(); }
    char peek(std::size_t ahead = 0) const { return index + ahead < source.size() ? source[index + ahead] : '\0'; }
    Position here() const { return {static_cast<std::uint32_t>(index), line, column}; }

    char advance() {
        char c = source[index++];
        if (c == '\n') { ++line; column = 1; }
        else if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++column;
        return c;
    }

    void push(TokenKind kind, std::string lexeme, std::string value, Position start) {
        Token token;
        token.kind = kind;
        token.lexeme = std::move(lexeme);
        token.value = std::move(value);
        token.range = {start, here()};
        result.tokens.push_back(std::move(token));
    }

    void error(std::string message, Position start) { result.errors.push_back({std::move(message), {start, here()}}); }

    void skipSpaces() {
        while (!atEnd()) {
            char c = peek();
            if (c == ' ' || c == '\t') advance();
            else break;
        }
    }

    void lineComment(Position start) {
        std::size_t begin = index;
        while (!atEnd() && peek() != '\n') advance();
        if (keep) push(TokenKind::comment, std::string(source.substr(begin, index - begin)), "", start);
    }

    void blockComment(Position start) {
        std::size_t begin = index;
        advance();
        advance();
        int depth = 1;
        while (!atEnd() && depth > 0) {
            if (peek() == '/' && peek(1) == '*') { advance(); advance(); ++depth; continue; }
            if (peek() == '*' && peek(1) == '/') { advance(); advance(); --depth; continue; }
            advance();
        }
        if (depth > 0) error(tr("Commento non terminato"), start);
        if (keep) push(TokenKind::comment, std::string(source.substr(begin, index - begin)), "", start);
    }

    void identifier(Position start) {
        std::size_t begin = index;
        while (!atEnd() && identifierPart(static_cast<unsigned char>(peek()))) advance();
        std::string word(source.substr(begin, index - begin));
        push(isKeyword(word) ? TokenKind::keyword : TokenKind::identifier, word, word, start);
    }

    void number(Position start) {
        std::size_t begin = index;
        std::string digits;
        bool real = false;
        if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
            advance();
            advance();
            while (!atEnd() && (std::isxdigit(static_cast<unsigned char>(peek())) || peek() == '_')) {
                char c = advance();
                if (c != '_') digits += c;
            }
            if (digits.empty()) error(tr("Numero esadecimale vuoto"), start);
            push(TokenKind::integer, std::string(source.substr(begin, index - begin)), "0x" + digits, start);
            return;
        }
        while (!atEnd() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')) {
            char c = advance();
            if (c != '_') digits += c;
        }
        if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1)))) {
            real = true;
            digits += advance();
            while (!atEnd() && (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '_')) {
                char c = advance();
                if (c != '_') digits += c;
            }
        }
        if ((peek() == 'e' || peek() == 'E') && (std::isdigit(static_cast<unsigned char>(peek(1))) || ((peek(1) == '+' || peek(1) == '-') && std::isdigit(static_cast<unsigned char>(peek(2)))))) {
            real = true;
            digits += advance();
            if (peek() == '+' || peek() == '-') digits += advance();
            while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) digits += advance();
        }
        if (!atEnd() && identifierStart(static_cast<unsigned char>(peek()))) {
            while (!atEnd() && identifierPart(static_cast<unsigned char>(peek()))) advance();
            error(tr("Numero non valido"), start);
        }
        push(real ? TokenKind::number : TokenKind::integer, std::string(source.substr(begin, index - begin)), digits, start);
    }

    void string(bool opening, Position start = {}) {
        if (opening) stringOrigin = start;
        Position segment = opening ? start : here();
        std::string value;
        while (true) {
            if (atEnd() || peek() == '\n') {
                error(tr("Testo non terminato"), stringOrigin);
                push(opening ? TokenKind::text : TokenKind::textEnd, "\"", value, segment);
                return;
            }
            char c = advance();
            if (c == '"') {
                push(opening ? TokenKind::text : TokenKind::textEnd, "\"", value, segment);
                return;
            }
            if (c != '\\') { value += c; continue; }
            if (atEnd()) continue;
            char e = advance();
            switch (e) {
            case 'n': value += '\n'; break;
            case 't': value += '\t'; break;
            case 'r': value += '\r'; break;
            case '0': value += '\0'; break;
            case '"': value += '"'; break;
            case '\\': value += '\\'; break;
            case '(':
                push(opening ? TokenKind::textStart : TokenKind::textMiddle, "\"", value, segment);
                interpolations.push_back(0);
                return;
            case 'u': {
                if (peek() != '{') { error(tr("Escape unicode non valido"), here()); break; }
                advance();
                std::string hex;
                while (!atEnd() && std::isxdigit(static_cast<unsigned char>(peek())) && hex.size() < 6) hex += advance();
                if (peek() != '}' || hex.empty()) { error(tr("Escape unicode non valido"), here()); break; }
                advance();
                auto code = static_cast<std::uint32_t>(std::stoul(hex, nullptr, 16));
                if (code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) { error(tr("Carattere unicode non valido"), here()); break; }
                appendUtf8(value, code);
                break;
            }
            default: error(std::string("Escape sconosciuto \\") + e, here()); break;
            }
        }
    }

    bool symbol(Position start) {
        for (const auto& s : symbols) {
            if (source.substr(index, s.size()) == s) {
                for (std::size_t i = 0; i < s.size(); ++i) advance();
                push(TokenKind::symbol, s, "", start);
                return true;
            }
        }
        return false;
    }
};

}

const std::vector<std::string>& keywords() { return words; }

bool isKeyword(std::string_view word) { return std::find(words.begin(), words.end(), word) != words.end(); }

LexResult lex(std::string_view source, bool keepComments) { return Lexer(source, keepComments).run(); }

}
