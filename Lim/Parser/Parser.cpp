#include "Parser.hpp"

#include "Localization/Localization.hpp"

#include <cerrno>
#include <cstdlib>
#include <functional>
#include <xlocale.h>

namespace lim {

using ld::tr;

namespace {

constexpr int maximumDepth = 200;

class Parser {
public:
    Parser(std::vector<Token> tokens, std::string file) : tokens(std::move(tokens)), file(std::move(file)) {}

    ParseResult run() {
        ParseResult result;
        result.program.file = file;
        skipSeparators();
        while (!check(TokenKind::end)) {
            std::size_t before = position;
            auto statement = parseStatement(true);
            if (statement) result.program.statements.push_back(std::move(statement));
            if (!endStatement()) synchronize();
            if (position == before) advance();
            skipSeparators();
        }
        result.diagnostics = std::move(diagnostics);
        result.tokens = std::move(tokens);
        return result;
    }

private:
    std::vector<Token> tokens;
    std::string file;
    std::size_t position = 0;
    int nesting = 0;
    int depth = 0;
    std::vector<Diagnostic> diagnostics;

    const Token& current() {
        if (nesting > 0)
            while (tokens[position].kind == TokenKind::newline) ++position;
        return tokens[position];
    }
    const Token& previous() const { return tokens[position > 0 ? position - 1 : 0]; }
    const Token& peekAhead(std::size_t n) {
        std::size_t index = position;
        std::size_t seen = 0;
        while (index < tokens.size()) {
            if (nesting > 0 && tokens[index].kind == TokenKind::newline) { ++index; continue; }
            if (seen == n) return tokens[index];
            ++seen;
            ++index;
        }
        return tokens.back();
    }
    bool check(TokenKind kind) { return current().kind == kind; }
    bool checkSymbol(std::string_view s) { return current().isSymbol(s); }
    bool checkKeyword(std::string_view s) { return current().isKeyword(s); }
    const Token& advance() {
        const Token& token = current();
        if (token.kind != TokenKind::end) ++position;
        return token;
    }
    bool matchSymbol(std::string_view s) {
        if (!checkSymbol(s)) return false;
        advance();
        return true;
    }
    bool matchKeyword(std::string_view s) {
        if (!checkKeyword(s)) return false;
        advance();
        return true;
    }

    void error(const std::string& message, Range range, std::string help = {}, std::string expected = {}, std::string actual = {}) {
        if (!diagnostics.empty() && diagnostics.back().range.start.offset == range.start.offset) return;
        Diagnostic d;
        d.code = "sintassi";
        d.message = message;
        d.file = file;
        d.range = range;
        d.help = std::move(help);
        d.expected = std::move(expected);
        d.actual = std::move(actual);
        diagnostics.push_back(std::move(d));
    }

    std::string describe(const Token& token) {
        switch (token.kind) {
        case TokenKind::end: return tr("fine del testo");
        case TokenKind::newline: return tr("fine della riga");
        case TokenKind::identifier: return tr("nome «{0}»", token.lexeme);
        case TokenKind::integer:
        case TokenKind::number: return "numero " + token.lexeme;
        case TokenKind::text:
        case TokenKind::textStart: return "testo";
        default: return "«" + token.lexeme + "»";
        }
    }

    bool expectSymbol(std::string_view s, const std::string& context) {
        if (matchSymbol(s)) return true;
        error(tr("Manca «{0}» {1}", std::string(s), context), current().range, {}, "«" + std::string(s) + "»", describe(current()));
        return false;
    }

    std::string expectIdentifier(const std::string& context, Range* where = nullptr) {
        if (check(TokenKind::identifier)) {
            if (where) *where = current().range;
            return advance().lexeme;
        }
        error(tr("Manca un nome {0}", context), current().range, {}, "nome", describe(current()));
        return {};
    }

    void skipSeparators() {
        while (tokens[position].kind == TokenKind::newline || tokens[position].isSymbol(";")) ++position;
    }

    bool endStatement() {
        const Token& token = tokens[position];
        if (token.kind == TokenKind::newline || token.isSymbol(";")) { ++position; return true; }
        if (token.kind == TokenKind::end || token.isSymbol("}")) return true;
        error(tr("Istruzione non terminata: vai a capo o usa «;»"), token.range, {}, tr("fine della riga"), describe(token));
        return false;
    }

    void synchronize() {
        int braces = 0;
        while (tokens[position].kind != TokenKind::end) {
            const Token& token = tokens[position];
            if (token.isSymbol("{")) ++braces;
            if (token.isSymbol("}")) {
                if (braces == 0) return;
                --braces;
            }
            if (token.kind == TokenKind::newline && braces == 0) { ++position; return; }
            ++position;
        }
    }

    Range span(const Range& start) { return {start.start, previous().range.end}; }

    struct DepthGuard {
        Parser& parser;
        bool ok;
        explicit DepthGuard(Parser& p) : parser(p), ok(++p.depth <= maximumDepth) {}
        ~DepthGuard() { --parser.depth; }
    };

    std::shared_ptr<Block> parseBlock(const std::string& context) {
        auto block = std::make_shared<Block>();
        Range start = current().range;
        if (!expectSymbol("{", context)) return block;
        int saved = nesting;
        nesting = 0;
        skipSeparators();
        while (!checkSymbol("}") && !check(TokenKind::end)) {
            std::size_t before = position;
            auto statement = parseStatement(false);
            if (statement) block->statements.push_back(std::move(statement));
            if (!endStatement()) synchronize();
            if (position == before) advance();
            skipSeparators();
        }
        nesting = saved;
        if (!matchSymbol("}")) error(tr("Blocco non chiuso: manca «}»"), start, tr("Ogni «{» deve avere il suo «}»."));
        block->range = span(start);
        return block;
    }

    TypeRef parseType() {
        DepthGuard guard(*this);
        if (!guard.ok) { error(tr("Tipo troppo annidato"), current().range); return nullptr; }
        auto type = std::make_shared<TypeExpr>();
        Range start = current().range;
        if (matchSymbol("[")) {
            ++nesting;
            auto first = parseType();
            if (matchSymbol(":")) {
                type->kind = TypeExpr::Kind::map;
                type->arguments = {first, parseType()};
            } else {
                type->kind = TypeExpr::Kind::list;
                type->arguments = {first};
            }
            --nesting;
            expectSymbol("]", tr("nel tipo"));
        } else if (matchKeyword("fn")) {
            type->kind = TypeExpr::Kind::function;
            expectSymbol("(", tr("nel tipo funzione"));
            ++nesting;
            if (!checkSymbol(")")) {
                do type->arguments.push_back(parseType());
                while (matchSymbol(","));
            }
            --nesting;
            expectSymbol(")", tr("nel tipo funzione"));
            if (matchSymbol("->")) type->result = parseType();
        } else if (check(TokenKind::identifier) || checkKeyword("none")) {
            type->kind = TypeExpr::Kind::named;
            type->name = advance().lexeme;
            if (type->name == "none") type->name = "None";
        } else {
            error(tr("Tipo atteso"), current().range, {}, "tipo", describe(current()));
            return nullptr;
        }
        type->range = span(start);
        while (checkSymbol("?")) {
            advance();
            auto wrapped = std::make_shared<TypeExpr>();
            wrapped->kind = TypeExpr::Kind::optional;
            wrapped->arguments = {type};
            wrapped->range = span(start);
            type = wrapped;
        }
        return type;
    }

    std::vector<Parameter> parseParameters() {
        std::vector<Parameter> parameters;
        expectSymbol("(", tr("prima dei parametri"));
        ++nesting;
        if (!checkSymbol(")")) {
            do {
                Parameter p;
                p.range = current().range;
                if (checkKeyword("self")) { advance(); error(tr("«self» non può essere un parametro"), p.range); continue; }
                p.name = expectIdentifier(tr("per il parametro"));
                if (matchSymbol(":")) p.type = parseType();
                if (matchSymbol("=")) p.fallback = parseRequired();
                p.range = span(p.range);
                parameters.push_back(std::move(p));
            } while (matchSymbol(","));
        }
        --nesting;
        expectSymbol(")", tr("dopo i parametri"));
        return parameters;
    }

    std::shared_ptr<FunctionDecl> parseFunction(bool async, bool requireName) {
        auto function = std::make_shared<FunctionDecl>();
        function->async = async;
        if (requireName) {
            if (check(TokenKind::identifier)) {
                function->nameRange = current().range;
                function->name = advance().lexeme;
            } else error(tr("Manca il nome della funzione"), current().range, tr("Scrivi per esempio: fn saluta() { }"));
        }
        function->parameters = parseParameters();
        if (matchSymbol("->")) function->returns = parseType();
        function->body = parseBlock(tr("per aprire il corpo della funzione"));
        return function;
    }

    StmtPtr parseStatement(bool topLevel) {
        DepthGuard guard(*this);
        if (!guard.ok) { error(tr("Codice troppo annidato"), current().range); return nullptr; }
        Range start = current().range;
        bool exported = false;
        if (checkKeyword("export")) {
            advance();
            exported = true;
            if (!topLevel) error(tr("«export» è consentito solo al livello principale di un modulo"), start);
        }
        if (checkKeyword("let") || checkKeyword("var")) {
            bool mutable_ = advance().lexeme == "var";
            auto s = std::make_unique<Stmt>(StmtKind::declaration, start);
            s->mutable_ = mutable_;
            s->exported = exported;
            s->name = expectIdentifier(mutable_ ? tr("dopo «var»") : tr("dopo «let»"), &s->nameRange);
            if (matchSymbol(":")) s->type = parseType();
            if (matchSymbol("=")) s->value = parseRequired();
            else if (!mutable_) error(tr("Una costante «let» deve avere un valore"), s->nameRange, tr("Usa «var» se il valore arriva dopo."));
            s->range = span(start);
            return s;
        }
        if (checkKeyword("async") && peekAhead(1).isKeyword("fn") && peekAhead(2).kind == TokenKind::identifier) {
            advance();
            advance();
            auto s = std::make_unique<Stmt>(StmtKind::function, start);
            s->exported = exported;
            s->function = parseFunction(true, true);
            s->name = s->function->name;
            s->nameRange = s->function->nameRange;
            s->range = span(start);
            return s;
        }
        if (checkKeyword("fn") && peekAhead(1).kind == TokenKind::identifier) {
            advance();
            auto s = std::make_unique<Stmt>(StmtKind::function, start);
            s->exported = exported;
            s->function = parseFunction(false, true);
            s->name = s->function->name;
            s->nameRange = s->function->nameRange;
            s->range = span(start);
            return s;
        }
        if (matchKeyword("class")) return parseClass(start, exported);
        if (matchKeyword("enum")) return parseEnum(start, exported);
        if (exported) error(tr("Dopo «export» serve let, var, fn, class o enum"), current().range);
        if (matchKeyword("import")) {
            auto s = std::make_unique<Stmt>(StmtKind::import, start);
            s->name = expectIdentifier(tr("del modulo da importare"), &s->nameRange);
            if (!topLevel) error(tr("«import» va scritto all’inizio dello script"), start);
            s->range = span(start);
            return s;
        }
        if (matchKeyword("if")) return parseIf(start);
        if (matchKeyword("while")) {
            auto s = std::make_unique<Stmt>(StmtKind::whileStmt, start);
            s->value = parseRequired();
            s->body = parseBlock(tr("dopo la condizione di «while»"));
            s->range = span(start);
            return s;
        }
        if (matchKeyword("for")) {
            auto s = std::make_unique<Stmt>(StmtKind::forStmt, start);
            Range r;
            s->names.push_back(expectIdentifier(tr("dopo «for»"), &r));
            s->nameRanges.push_back(r);
            if (matchSymbol(",")) {
                s->names.push_back(expectIdentifier(tr("dopo la virgola"), &r));
                s->nameRanges.push_back(r);
            }
            if (!matchKeyword("in")) error(tr("Manca «in» nel ciclo for"), current().range, tr("Scrivi per esempio: for player in Players.all { }"));
            s->value = parseRequired();
            s->body = parseBlock(tr("dopo l’intestazione di «for»"));
            s->range = span(start);
            return s;
        }
        if (matchKeyword("match")) return parseMatch(start);
        if (matchKeyword("return")) {
            auto s = std::make_unique<Stmt>(StmtKind::returnStmt, start);
            if (!check(TokenKind::newline) && !checkSymbol("}") && !checkSymbol(";") && !check(TokenKind::end)) s->value = parseRequired();
            s->range = span(start);
            return s;
        }
        if (matchKeyword("break")) return std::make_unique<Stmt>(StmtKind::breakStmt, start);
        if (matchKeyword("continue")) return std::make_unique<Stmt>(StmtKind::continueStmt, start);
        if (matchKeyword("on")) return parseOn(start);
        if (checkSymbol("{")) {
            auto s = std::make_unique<Stmt>(StmtKind::block, start);
            s->body = parseBlock("");
            s->range = span(start);
            return s;
        }
        auto expression = parseExpression();
        if (!expression) return nullptr;
        for (const char* op : {"=", "+=", "-=", "*=", "/=", "%="}) {
            if (checkSymbol(op)) {
                Range opRange = current().range;
                advance();
                if (expression->kind != ExprKind::identifier && expression->kind != ExprKind::member && expression->kind != ExprKind::index) {
                    error(tr("Si può assegnare solo a una variabile, a una proprietà o a un elemento"), expression->range);
                }
                if (expression->optionalChain) error(tr("Non si può assegnare attraverso «?.»"), opRange);
                auto s = std::make_unique<Stmt>(StmtKind::assign, start);
                s->op = op;
                s->target = std::move(expression);
                s->value = parseRequired();
                s->range = span(start);
                return s;
            }
        }
        auto s = std::make_unique<Stmt>(StmtKind::expression, start);
        s->value = std::move(expression);
        s->range = span(start);
        return s;
    }

    StmtPtr parseIf(Range start) {
        auto s = std::make_unique<Stmt>(StmtKind::ifStmt, start);
        s->value = parseRequired();
        s->body = parseBlock(tr("dopo la condizione di «if»"));
        std::size_t saved = position;
        skipSeparators();
        if (checkKeyword("else")) {
            advance();
            if (checkKeyword("if")) {
                Range inner = current().range;
                advance();
                auto nested = parseIf(inner);
                auto block = std::make_shared<Block>();
                block->range = nested->range;
                block->statements.push_back(std::move(nested));
                s->otherwise = block;
            } else s->otherwise = parseBlock(tr("dopo «else»"));
        } else position = saved;
        s->range = span(start);
        return s;
    }

    StmtPtr parseMatch(Range start) {
        auto s = std::make_unique<Stmt>(StmtKind::matchStmt, start);
        s->value = parseRequired();
        Range open = current().range;
        if (!expectSymbol("{", tr("per aprire «match»"))) return s;
        skipSeparators();
        while (!checkSymbol("}") && !check(TokenKind::end)) {
            std::size_t before = position;
            MatchCase c;
            c.range = current().range;
            if (matchKeyword("else")) c.otherwise = true;
            else {
                do {
                    ++nesting;
                    auto pattern = parseExpression();
                    --nesting;
                    if (pattern) c.patterns.push_back(std::move(pattern));
                } while (matchSymbol(","));
            }
            c.body = parseBlock(tr("per il caso di «match»"));
            c.range = span(c.range);
            s->cases.push_back(std::move(c));
            if (position == before) advance();
            skipSeparators();
        }
        if (!matchSymbol("}")) error(tr("«match» non chiuso: manca «}»"), open);
        s->range = span(start);
        return s;
    }

    StmtPtr parseOn(Range start) {
        auto s = std::make_unique<Stmt>(StmtKind::on, start);
        auto event = parsePostfix();
        if (!event) return nullptr;
        if (event->kind == ExprKind::call && event->child(0) && event->child(0)->kind == ExprKind::member) {
            for (auto& argument : event->arguments) {
                Parameter p;
                p.range = argument.range;
                if (!argument.label.empty()) {
                    p.name = argument.label;
                    if (argument.value && argument.value->kind == ExprKind::identifier) {
                        auto type = std::make_shared<TypeExpr>();
                        type->name = argument.value->text;
                        type->range = argument.value->range;
                        p.type = type;
                    } else error(tr("Tipo del parametro non valido"), argument.range);
                } else if (argument.value && argument.value->kind == ExprKind::identifier) p.name = argument.value->text;
                else error(tr("I parametri di «on» devono essere nomi"), argument.range, tr("Scrivi per esempio: on part.touched(hit) { }"));
                s->parameters.push_back(std::move(p));
            }
            s->target = std::move(event->children[0]);
        } else if (event->kind == ExprKind::member) s->target = std::move(event);
        else {
            error(tr("Dopo «on» serve un evento, per esempio part.touched"), event->range);
            s->target = std::move(event);
        }
        s->body = parseBlock(tr("per il corpo dell’evento"));
        s->range = span(start);
        return s;
    }

    StmtPtr parseClass(Range start, bool exported) {
        auto s = std::make_unique<Stmt>(StmtKind::classDecl, start);
        s->exported = exported;
        s->name = expectIdentifier(tr("della classe"), &s->nameRange);
        Range open = current().range;
        if (!expectSymbol("{", tr("per aprire la classe"))) return s;
        skipSeparators();
        while (!checkSymbol("}") && !check(TokenKind::end)) {
            std::size_t before = position;
            Range memberStart = current().range;
            if (checkKeyword("var") || checkKeyword("let")) {
                Field f;
                f.mutable_ = advance().lexeme == "var";
                f.name = expectIdentifier(tr("del campo"), &f.range);
                if (matchSymbol(":")) f.type = parseType();
                if (matchSymbol("=")) f.initializer = parseRequired();
                s->fields.push_back(std::move(f));
            } else if (checkKeyword("fn") || (checkKeyword("async") && peekAhead(1).isKeyword("fn"))) {
                bool async = matchKeyword("async");
                advance();
                auto method = parseFunction(async, true);
                method->isInit = method->name == "init";
                s->methods.push_back(method);
            } else {
                error(tr("In una classe servono campi (var/let) o metodi (fn)"), memberStart);
                synchronize();
            }
            if (!checkSymbol("}")) endStatement();
            if (position == before) advance();
            skipSeparators();
        }
        if (!matchSymbol("}")) error(tr("Classe non chiusa: manca «}»"), open);
        s->range = span(start);
        return s;
    }

    StmtPtr parseEnum(Range start, bool exported) {
        auto s = std::make_unique<Stmt>(StmtKind::enumDecl, start);
        s->exported = exported;
        s->name = expectIdentifier("dell'enum", &s->nameRange);
        Range open = current().range;
        if (!expectSymbol("{", tr("per aprire l’enum"))) return s;
        skipSeparators();
        while (!checkSymbol("}") && !check(TokenKind::end)) {
            std::size_t before = position;
            Range r;
            std::string name = expectIdentifier(tr("del caso"), &r);
            if (name.empty()) {
                synchronize();
                if (position == before) advance();
                skipSeparators();
                continue;
            }
            s->enumCases.emplace_back(name, r);
            matchSymbol(",");
            skipSeparators();
        }
        if (!matchSymbol("}")) error(tr("Enum non chiuso: manca «}»"), open);
        s->range = span(start);
        return s;
    }

    ExprPtr parseExpression() { return parseBinary(0); }

    ExprPtr parseRequired() {
        Range at = current().range;
        auto parsed = parseExpression();
        if (parsed) return parsed;
        error(tr("Manca un’espressione"), at);
        return std::make_unique<Expr>(ExprKind::invalid, at);
    }

    static int precedence(const Token& token) {
        if (token.kind == TokenKind::keyword) {
            if (token.lexeme == "or") return 2;
            if (token.lexeme == "and") return 3;
            if (token.lexeme == "is") return 4;
            if (token.lexeme == "as") return 9;
            return -1;
        }
        if (token.kind != TokenKind::symbol) return -1;
        const std::string& s = token.lexeme;
        if (s == "??") return 1;
        if (s == "||") return 2;
        if (s == "&&") return 3;
        if (s == "==" || s == "!=") return 4;
        if (s == "<" || s == "<=" || s == ">" || s == ">=") return 5;
        if (s == "..." || s == "..<") return 6;
        if (s == "+" || s == "-") return 7;
        if (s == "*" || s == "/" || s == "%") return 8;
        return -1;
    }

    ExprPtr parseBinary(int minimum) {
        DepthGuard guard(*this);
        if (!guard.ok) { error(tr("Espressione troppo annidata"), current().range); return nullptr; }
        auto left = parseUnary();
        if (!left) return nullptr;
        while (true) {
            const Token& token = current();
            int p = precedence(token);
            if (p < 0 || p < minimum) break;
            std::string op = token.lexeme == "or" ? "||" : token.lexeme == "and" ? "&&" : token.lexeme;
            advance();
            if (op == "is" || op == "as") {
                auto node = std::make_unique<Expr>(op == "is" ? ExprKind::is : ExprKind::cast, left->range);
                node->type = parseType();
                node->children.push_back(std::move(left));
                node->range = span(node->range);
                left = std::move(node);
                continue;
            }
            int next = op == "??" ? p : p + 1;
            auto right = parseBinary(next);
            if (!right) {
                error(tr("Manca l’operando dopo «{0}»", op), current().range);
                return left;
            }
            auto node = std::make_unique<Expr>(ExprKind::binary, left->range);
            node->op = op;
            node->children.push_back(std::move(left));
            node->children.push_back(std::move(right));
            node->range = span(node->range);
            left = std::move(node);
        }
        return left;
    }

    ExprPtr parseUnary() {
        Range start = current().range;
        if (checkSymbol("-") || checkSymbol("!") || checkKeyword("not")) {
            std::string op = advance().lexeme == "not" ? "!" : previous().lexeme;
            DepthGuard guard(*this);
            if (!guard.ok) { error(tr("Espressione troppo annidata"), start); return nullptr; }
            auto operand = parseUnary();
            if (!operand) return nullptr;
            auto node = std::make_unique<Expr>(ExprKind::unary, start);
            node->op = op;
            node->children.push_back(std::move(operand));
            node->range = span(start);
            return node;
        }
        if (matchKeyword("await")) {
            DepthGuard guard(*this);
            if (!guard.ok) { error(tr("Espressione troppo annidata"), start); return nullptr; }
            auto operand = parseUnary();
            if (!operand) return nullptr;
            auto node = std::make_unique<Expr>(ExprKind::await, start);
            node->children.push_back(std::move(operand));
            node->range = span(start);
            return node;
        }
        return parsePostfix();
    }

    std::vector<Argument> parseArguments() {
        std::vector<Argument> arguments;
        ++nesting;
        if (!checkSymbol(")")) {
            do {
                if (checkSymbol(")")) break;
                Argument a;
                a.range = current().range;
                if (check(TokenKind::identifier) && peekAhead(1).isSymbol(":")) {
                    a.label = advance().lexeme;
                    advance();
                }
                a.value = parseRequired();
                a.range = span(a.range);
                arguments.push_back(std::move(a));
            } while (matchSymbol(","));
        }
        --nesting;
        expectSymbol(")", tr("per chiudere la chiamata"));
        return arguments;
    }

    ExprPtr parsePostfix() {
        auto expression = parsePrimary();
        if (!expression) return nullptr;
        while (true) {
            if (checkSymbol("(")) {
                Range start = expression->range;
                advance();
                auto call = std::make_unique<Expr>(ExprKind::call, start);
                call->arguments = parseArguments();
                call->children.push_back(std::move(expression));
                call->range = span(start);
                expression = std::move(call);
                continue;
            }
            if (checkSymbol(".") || checkSymbol("?.")) {
                bool optional = advance().lexeme == "?.";
                Range start = expression->range;
                auto member = std::make_unique<Expr>(ExprKind::member, start);
                member->optionalChain = optional;
                if (check(TokenKind::identifier) || check(TokenKind::keyword)) member->text = advance().lexeme;
                else error(tr("Manca il nome del membro dopo «.»"), current().range, tr("Scrivi per esempio: part.position"));
                member->children.push_back(std::move(expression));
                member->range = span(start);
                expression = std::move(member);
                continue;
            }
            if (checkSymbol("[")) {
                Range start = expression->range;
                advance();
                ++nesting;
                auto index = std::make_unique<Expr>(ExprKind::index, start);
                index->children.push_back(std::move(expression));
                index->children.push_back(parseRequired());
                --nesting;
                expectSymbol("]", tr("per chiudere l’indice"));
                index->range = span(start);
                expression = std::move(index);
                continue;
            }
            break;
        }
        return expression;
    }

    ExprPtr parsePrimary() {
        const Token& token = current();
        Range start = token.range;
        switch (token.kind) {
        case TokenKind::integer: {
            advance();
            auto node = std::make_unique<Expr>(ExprKind::integer, start);
            const std::string& digits = token.value;
            errno = 0;
            char* end = nullptr;
            long long value = startsWithHex(digits) ? std::strtoll(digits.c_str() + 2, &end, 16) : std::strtoll(digits.c_str(), &end, 10);
            if (errno == ERANGE) {
                node->kind = ExprKind::number;
                node->number = strtod_l(digits.c_str(), nullptr, cLocale());
            } else node->integer = value;
            return node;
        }
        case TokenKind::number: {
            advance();
            auto node = std::make_unique<Expr>(ExprKind::number, start);
            node->number = strtod_l(token.value.c_str(), nullptr, cLocale());
            return node;
        }
        case TokenKind::text: {
            advance();
            auto node = std::make_unique<Expr>(ExprKind::text, start);
            node->text = token.value;
            return node;
        }
        case TokenKind::textStart: return parseInterpolation();
        case TokenKind::identifier: {
            advance();
            auto node = std::make_unique<Expr>(ExprKind::identifier, start);
            node->text = token.lexeme;
            return node;
        }
        case TokenKind::keyword: {
            if (token.lexeme == "true" || token.lexeme == "false") {
                advance();
                auto node = std::make_unique<Expr>(ExprKind::boolean, start);
                node->flag = token.lexeme == "true";
                return node;
            }
            if (token.lexeme == "none") { advance(); return std::make_unique<Expr>(ExprKind::none, start); }
            if (token.lexeme == "self") { advance(); return std::make_unique<Expr>(ExprKind::self, start); }
            if (token.lexeme == "fn" || (token.lexeme == "async" && peekAhead(1).isKeyword("fn"))) {
                bool async = token.lexeme == "async";
                advance();
                if (async) advance();
                auto node = std::make_unique<Expr>(ExprKind::lambda, start);
                node->async = async;
                int saved = nesting;
                nesting = 0;
                auto function = parseFunction(async, false);
                nesting = saved;
                node->parameters = std::move(function->parameters);
                node->returns = function->returns;
                node->body = function->body;
                node->range = span(start);
                return node;
            }
            break;
        }
        case TokenKind::symbol: {
            if (token.lexeme == "(") {
                advance();
                ++nesting;
                auto inner = parseExpression();
                --nesting;
                expectSymbol(")", tr("per chiudere la parentesi"));
                if (inner) inner->range = span(start);
                return inner;
            }
            if (token.lexeme == "[") return parseCollection();
            if (token.lexeme == "." && peekAhead(1).kind == TokenKind::identifier) {
                advance();
                auto node = std::make_unique<Expr>(ExprKind::shorthand, start);
                node->text = advance().lexeme;
                node->range = span(start);
                return node;
            }
            break;
        }
        default: break;
        }
        error(tr("Espressione attesa"), token.range, {}, tr("valore o espressione"), describe(token));
        return nullptr;
    }

    static bool startsWithHex(const std::string& digits) { return digits.size() > 2 && digits[0] == '0' && digits[1] == 'x'; }
    static locale_t cLocale() {
        static locale_t locale = newlocale(LC_ALL_MASK, "C", nullptr);
        return locale;
    }

    ExprPtr parseInterpolation() {
        Range start = current().range;
        auto node = std::make_unique<Expr>(ExprKind::interpolation, start);
        node->parts.push_back(advance().value);
        while (true) {
            int saved = nesting;
            nesting = 1;
            auto inner = parseExpression();
            nesting = saved;
            if (inner) node->children.push_back(std::move(inner));
            else node->children.push_back(std::make_unique<Expr>(ExprKind::text, current().range));
            if (check(TokenKind::textMiddle)) {
                node->parts.push_back(advance().value);
                continue;
            }
            if (check(TokenKind::textEnd)) {
                node->parts.push_back(advance().value);
                break;
            }
            error(tr("Interpolazione non chiusa: manca «)»"), current().range);
            node->parts.push_back("");
            break;
        }
        node->range = span(start);
        return node;
    }

    ExprPtr parseCollection() {
        Range start = current().range;
        advance();
        ++nesting;
        if (matchSymbol(":")) {
            --nesting;
            expectSymbol("]", tr("per chiudere la mappa vuota"));
            auto node = std::make_unique<Expr>(ExprKind::map, start);
            node->range = span(start);
            return node;
        }
        if (matchSymbol("]")) {
            --nesting;
            auto node = std::make_unique<Expr>(ExprKind::list, start);
            node->range = span(start);
            return node;
        }
        auto first = parseRequired();
        if (matchSymbol(":")) {
            auto node = std::make_unique<Expr>(ExprKind::map, start);
            node->entries.emplace_back(std::move(first), parseRequired());
            while (matchSymbol(",")) {
                if (checkSymbol("]")) break;
                auto key = parseRequired();
                expectSymbol(":", tr("tra chiave e valore"));
                node->entries.emplace_back(std::move(key), parseRequired());
            }
            --nesting;
            expectSymbol("]", tr("per chiudere la mappa"));
            node->range = span(start);
            return node;
        }
        auto node = std::make_unique<Expr>(ExprKind::list, start);
        node->children.push_back(std::move(first));
        while (matchSymbol(",")) {
            if (checkSymbol("]")) break;
            node->children.push_back(parseRequired());
        }
        --nesting;
        expectSymbol("]", tr("per chiudere la lista"));
        node->range = span(start);
        return node;
    }
};

}

ParseResult parse(std::string_view source, std::string file) {
    auto lexed = lex(source);
    std::vector<Diagnostic> lexical;
    for (const auto& e : lexed.errors) {
        Diagnostic d;
        d.code = "lessico";
        d.message = e.message;
        d.file = file;
        d.range = e.range;
        lexical.push_back(d);
    }
    auto result = Parser(std::move(lexed.tokens), file).run();
    result.diagnostics.insert(result.diagnostics.begin(), lexical.begin(), lexical.end());
    return result;
}

}
