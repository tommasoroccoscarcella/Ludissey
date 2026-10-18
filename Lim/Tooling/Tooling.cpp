#include "Tooling.hpp"

#include "Lim/Lexer/Lexer.hpp"
#include "Lim/Parser/Parser.hpp"
#include "Localization/Localization.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>

namespace lim::tooling {

using ld::tr;

namespace {

bool identifierCharacter(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || static_cast<unsigned char>(c) >= 0x80; }

bool isOperator(const std::string& lexeme) {
    static const std::set<std::string> operators = {"...", "..<", "?.", "??", "==", "!=", "<=", ">=", "&&", "||", "->", "+=", "-=", "*=", "/=", "%=", "+", "-", "*", "/", "%", "=", "<", ">", "!", "?"};
    return operators.count(lexeme) > 0;
}

}

std::vector<Span> highlight(std::string_view source) {
    auto lexed = lex(source, true);
    std::vector<Span> spans;
    const auto& tokens = lexed.tokens;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (t.kind == TokenKind::newline || t.kind == TokenKind::end) continue;
        Span s;
        s.offset = t.range.start.offset;
        s.length = t.range.end.offset - t.range.start.offset;
        switch (t.kind) {
        case TokenKind::keyword: s.style = TokenStyle::keyword; break;
        case TokenKind::integer:
        case TokenKind::number: s.style = TokenStyle::number; break;
        case TokenKind::text:
        case TokenKind::textStart:
        case TokenKind::textMiddle:
        case TokenKind::textEnd: s.style = TokenStyle::text; break;
        case TokenKind::comment: s.style = TokenStyle::comment; break;
        case TokenKind::invalid: s.style = TokenStyle::invalid; break;
        case TokenKind::symbol: s.style = isOperator(t.lexeme) ? TokenStyle::operatorSymbol : TokenStyle::punctuation; break;
        case TokenKind::identifier: {
            bool afterDot = i > 0 && (tokens[i - 1].isSymbol(".") || tokens[i - 1].isSymbol("?."));
            bool call = i + 1 < tokens.size() && tokens[i + 1].isSymbol("(");
            if (!t.lexeme.empty() && std::isupper(static_cast<unsigned char>(t.lexeme[0]))) s.style = TokenStyle::type;
            else if (call) s.style = TokenStyle::function;
            else if (afterDot) s.style = TokenStyle::property;
            else s.style = TokenStyle::identifier;
            break;
        }
        default: s.style = TokenStyle::identifier; break;
        }
        if (s.length > 0) spans.push_back(s);
    }
    return spans;
}

namespace {

bool opensText(const Token& t) { return t.kind == TokenKind::textStart || t.kind == TokenKind::textMiddle; }

bool closesText(const Token& t) { return t.kind == TokenKind::textMiddle || t.kind == TokenKind::textEnd; }

bool valueEnd(const Token& t) {
    if (t.kind == TokenKind::identifier || t.kind == TokenKind::integer || t.kind == TokenKind::number || t.kind == TokenKind::text || t.kind == TokenKind::textEnd) return true;
    if (t.kind == TokenKind::keyword) return t.lexeme == "self" || t.lexeme == "none" || t.lexeme == "true" || t.lexeme == "false";
    return t.isSymbol(")") || t.isSymbol("]") || t.isSymbol("?");
}

bool spaced(const Token* before, const Token& a, const Token& b) {
    if (opensText(a) || closesText(b)) return false;
    if (b.kind == TokenKind::comment) return true;
    static const std::set<std::string> tight = {",", ")", "]", ".", "?.", ";", ":"};
    if (b.kind == TokenKind::symbol && tight.count(b.lexeme)) return false;
    if (a.isSymbol("(") || a.isSymbol("[") || a.isSymbol(".") || a.isSymbol("?.") || a.isSymbol("@") || a.isSymbol("!")) return false;
    if (a.isSymbol("...") || a.isSymbol("..<") || b.isSymbol("...") || b.isSymbol("..<")) return false;
    if ((b.isSymbol("(") || b.isSymbol("[")) && valueEnd(a) && !(a.kind == TokenKind::keyword && a.lexeme != "self")) return false;
    if (b.isSymbol("?") && valueEnd(a)) return false;
    if (a.isSymbol("-") || a.isSymbol("+")) {
        bool unary = !before || !valueEnd(*before);
        if (unary) return false;
    }
    if (b.isSymbol("(") && a.kind == TokenKind::keyword && a.lexeme == "fn") return true;
    return true;
}

}

FormatResult format(std::string_view source) {
    FormatResult result;
    auto parsed = parse(source, "script");
    for (const auto& d : parsed.diagnostics)
        if (d.severity == Severity::error) result.diagnostics.push_back(d);
    if (!result.diagnostics.empty()) {
        result.text = std::string(source);
        return result;
    }
    auto lexed = lex(source, true);
    std::vector<std::vector<const Token*>> lines;
    std::vector<bool> gaps;
    std::uint32_t lastLine = 0;
    for (const auto& t : lexed.tokens) {
        if (t.kind == TokenKind::end) break;
        if (t.kind == TokenKind::newline) continue;
        if (lines.empty() || t.range.start.line != lastLine) {
            gaps.push_back(!lines.empty() && t.range.start.line > lastLine + 1);
            lines.emplace_back();
        }
        lines.back().push_back(&t);
        lastLine = t.range.end.line;
    }
    std::string out;
    int depth = 0;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        const auto& line = lines[index];
        int leading = 0;
        for (const Token* t : line) {
            if (t->isSymbol("}") || t->isSymbol(")") || t->isSymbol("]")) ++leading;
            else break;
        }
        int indent = std::max(0, depth - leading);
        if (gaps[index]) out += "\n";
        out += std::string(static_cast<std::size_t>(indent) * 4, ' ');
        const Token* previous = nullptr;
        const Token* beforePrevious = nullptr;
        for (const Token* t : line) {
            if (previous && spaced(beforePrevious, *previous, *t)) out += " ";
            if (closesText(*t)) out += ")";
            out += std::string(source.substr(t->range.start.offset, t->range.end.offset - t->range.start.offset));
            if (t->isSymbol("{") || t->isSymbol("(") || t->isSymbol("[") || opensText(*t)) ++depth;
            if (t->isSymbol("}") || t->isSymbol(")") || t->isSymbol("]") || closesText(*t)) --depth;
            beforePrevious = previous;
            previous = t;
        }
        out += "\n";
    }
    auto significant = [](std::string_view text) {
        std::vector<std::string> list;
        for (const auto& t : lex(text, true).tokens)
            if (t.kind != TokenKind::newline && t.kind != TokenKind::end) list.push_back(std::string(text.substr(t.range.start.offset, t.range.end.offset - t.range.start.offset)));
        return list;
    };
    if (significant(out) != significant(source)) {
        result.text = std::string(source);
        return result;
    }
    result.changed = out != source;
    result.text = std::move(out);
    return result;
}

namespace {

struct Analyzed {
    ParseResult parsed;
    Analysis analysis;
};

Analyzed analyzeSource(std::string_view source, const Context& context) {
    Analyzed a;
    a.parsed = parse(source, context.options.file);
    a.analysis = analyze(a.parsed.program, context.registry, context.options);
    return a;
}

void walkExpr(const Expr* e, const std::function<void(const Expr&)>& visit);
void walkBlock(const Block* b, const std::function<void(const Expr&)>& visit);

void walkStmt(const Stmt* s, const std::function<void(const Expr&)>& visit) {
    if (!s) return;
    walkExpr(s->value.get(), visit);
    walkExpr(s->target.get(), visit);
    walkBlock(s->body.get(), visit);
    walkBlock(s->otherwise.get(), visit);
    for (const auto& c : s->cases) {
        for (const auto& p : c.patterns) walkExpr(p.get(), visit);
        walkBlock(c.body.get(), visit);
    }
    if (s->function) {
        for (const auto& p : s->function->parameters) walkExpr(p.fallback.get(), visit);
        walkBlock(s->function->body.get(), visit);
    }
    for (const auto& f : s->fields) walkExpr(f.initializer.get(), visit);
    for (const auto& m : s->methods) {
        for (const auto& p : m->parameters) walkExpr(p.fallback.get(), visit);
        walkBlock(m->body.get(), visit);
    }
    for (const auto& p : s->parameters) walkExpr(p.fallback.get(), visit);
}

void walkBlock(const Block* b, const std::function<void(const Expr&)>& visit) {
    if (!b) return;
    for (const auto& s : b->statements) walkStmt(s.get(), visit);
}

void walkExpr(const Expr* e, const std::function<void(const Expr&)>& visit) {
    if (!e) return;
    visit(*e);
    for (const auto& c : e->children) walkExpr(c.get(), visit);
    for (const auto& [k, v] : e->entries) {
        walkExpr(k.get(), visit);
        walkExpr(v.get(), visit);
    }
    for (const auto& a : e->arguments) walkExpr(a.value.get(), visit);
    for (const auto& p : e->parameters) walkExpr(p.fallback.get(), visit);
    walkBlock(e->body.get(), visit);
}

void walkProgram(const Program& program, const std::function<void(const Expr&)>& visit) {
    for (const auto& s : program.statements) walkStmt(s.get(), visit);
}

CompletionKind symbolKind(SymbolKind kind) {
    switch (kind) {
    case SymbolKind::variable:
    case SymbolKind::parameter: return CompletionKind::variable;
    case SymbolKind::constant: return CompletionKind::constant;
    case SymbolKind::function: return CompletionKind::function;
    case SymbolKind::classType:
    case SymbolKind::enumType: return CompletionKind::type;
    case SymbolKind::enumCase: return CompletionKind::enumCase;
    case SymbolKind::field: return CompletionKind::property;
    case SymbolKind::method: return CompletionKind::method;
    case SymbolKind::module: return CompletionKind::module;
    case SymbolKind::global:
    case SymbolKind::builtin: return CompletionKind::variable;
    }
    return CompletionKind::variable;
}

std::string describeMethod(const HostMethod& m) {
    std::string text = m.async ? "async fn " : "fn ";
    text += m.name + "(";
    for (std::size_t i = 0; i < m.parameterNames.size(); ++i) {
        if (i) text += ", ";
        text += m.parameterNames[i];
        if (i < m.parameters.size() && m.parameters[i]) text += ": " + m.parameters[i]->describe();
    }
    text += ")";
    if (m.result && m.result->kind != Type::Kind::none) text += " -> " + m.result->describe();
    return text;
}

void membersOf(const TypePtr& raw, const Context& context, const Analysis& analysis, std::vector<Completion>& out) {
    TypePtr owner = unwrapOptional(raw);
    if (!owner) return;
    const TypeRegistry& r = context.registry;
    if (isClassReference(owner)) {
        if (const HostClass* c = r.findClass(classReferenceName(owner))) {
            for (const auto& [name, m] : c->statics) out.push_back({name, CompletionKind::method, describeMethod(m), name + "(", m.doc});
            for (const auto& [name, k] : c->constants) out.push_back({name, CompletionKind::constant, k.type ? k.type->describe() : "", name, k.doc});
        }
        return;
    }
    if (owner->kind == Type::Kind::enumeration && !owner->name.empty() && owner->name[0] == '@') {
        std::string name = owner->name.substr(1);
        const std::vector<std::string>* cases = nullptr;
        if (auto it = analysis.enums.find(name); it != analysis.enums.end()) cases = &it->second;
        else cases = r.findEnum(name);
        if (cases)
            for (const auto& c : *cases) out.push_back({c, CompletionKind::enumCase, name, c, ""});
        out.push_back({"all", CompletionKind::property, "[" + name + "]", "all", tr("Tutti i casi.")});
        return;
    }
    if (owner->kind == Type::Kind::host) {
        for (const auto* p : r.properties(owner->name)) out.push_back({p->name, CompletionKind::property, p->type ? p->type->describe() : "", p->name, p->doc});
        for (const auto* m : r.methods(owner->name)) out.push_back({m->name, CompletionKind::method, describeMethod(*m), m->name + "(", m->doc});
        for (const auto* e : r.events(owner->name)) out.push_back({e->name, CompletionKind::event, "Evento", e->name, e->doc});
        return;
    }
    if (owner->kind == Type::Kind::user) {
        if (auto it = analysis.classes.find(owner->name); it != analysis.classes.end()) {
            for (const auto& [name, type] : it->second.fields) out.push_back({name, CompletionKind::property, type ? type->describe() : "", name, ""});
            for (const auto& [name, type] : it->second.methods) out.push_back({name, CompletionKind::method, type ? type->describe() : "", name + "(", ""});
        }
        return;
    }
    if (owner->kind == Type::Kind::module) {
        for (const auto& [name, type] : analysis.interface.exports) out.push_back({name, CompletionKind::variable, type ? type->describe() : "", name, ""});
    }
    for (const auto& name : builtinMembers(owner)) {
        TypePtr t = builtinMember(owner, name);
        bool function = t && t->kind == Type::Kind::function;
        out.push_back({name, function ? CompletionKind::method : CompletionKind::property, t ? t->describe() : "", function ? name + "(" : name, ""});
    }
}

bool visible(const Analysis& analysis, const Symbol& symbol, std::uint32_t offset) {
    if (symbol.external) return false;
    if (symbol.kind == SymbolKind::function || symbol.kind == SymbolKind::classType || symbol.kind == SymbolKind::enumType) return true;
    return symbol.range.end.offset <= offset;
}

std::optional<int> symbolAt(const Analysis& analysis, std::uint32_t offset) {
    for (const auto& ref : analysis.references)
        if (ref.range.start.offset <= offset && offset <= ref.range.end.offset) return ref.symbol;
    for (const auto& s : analysis.symbols)
        if (!s.external && s.range.start.offset <= offset && offset <= s.range.end.offset && s.range.end.offset > s.range.start.offset) return s.id;
    return std::nullopt;
}

std::string symbolTitle(const Symbol& s) {
    std::string prefix;
    switch (s.kind) {
    case SymbolKind::variable: prefix = "var "; break;
    case SymbolKind::constant: prefix = "let "; break;
    case SymbolKind::parameter: prefix = "parametro "; break;
    case SymbolKind::function: prefix = "fn "; break;
    case SymbolKind::classType: prefix = "class "; break;
    case SymbolKind::enumType: prefix = "enum "; break;
    case SymbolKind::field: prefix = "campo "; break;
    case SymbolKind::method: prefix = "metodo "; break;
    default: break;
    }
    std::string type = s.type ? s.type->describe() : "";
    if (s.kind == SymbolKind::classType || s.kind == SymbolKind::enumType) return prefix + s.name;
    return prefix + s.name + (type.empty() ? "" : ": " + type);
}

}

const std::vector<Snippet>& snippets() {
    static const std::vector<Snippet> list = {
        {"fn", "fn", tr("fn nome(parametro) {\n    \n}"), "Funzione"},
        {"async fn", "async", tr("async fn nome() {\n    await Task.wait(1)\n}"), tr("Funzione asincrona")},
        {"on", "on", tr("on oggetto.evento(valore) {\n    \n}"), tr("Reagisci a un evento")},
        {"if", "if", "if condizione {\n    \n}", "Condizione"},
        {"if else", "ife", "if condizione {\n    \n} else {\n    \n}", tr("Condizione con alternativa")},
        {"for in", "for", tr("for elemento in lista {\n    \n}"), tr("Ciclo su una lista")},
        {"for range", "fori", "for i in 0..<10 {\n    \n}", tr("Ciclo su un intervallo")},
        {"while", "while", "while condizione {\n    \n}", tr("Ciclo con condizione")},
        {"match", "match", tr("match valore {\n    .caso { }\n    _ { }\n}"), tr("Scelta tra casi")},
        {"class", "class", tr("class Nome {\n    var valore: Number = 0\n\n    fn metodo() {\n        \n    }\n}"), "Classe"},
        {"enum", "enum", "enum Stato {\n    primo\n    secondo\n}", "Enumerazione"},
        {"touched", "touched", tr("on parte.touched(colpito) {\n    let giocatore = Players.getPlayerFromCharacter(colpito.parent)\n}"), tr("Tocco su una parte")},
        {"playerAdded", "player", tr("on Players.playerAdded(giocatore) {\n    print(\"\\(giocatore.name) è entrato\")\n}"), tr("Nuovo giocatore")},
        {"remote", "remote", tr("let evento = Network.event(\"Nome\")\nif evento != none {\n    on evento.onServerEvent(giocatore, dati) {\n        \n    }\n}"), tr("Evento di rete sul server")},
    };
    return list;
}

std::vector<Completion> complete(std::string_view source, std::uint32_t offset, const Context& context) {
    offset = std::min<std::uint32_t>(offset, static_cast<std::uint32_t>(source.size()));
    std::uint32_t start = offset;
    while (start > 0 && identifierCharacter(source[start - 1])) --start;
    std::string prefix(source.substr(start, offset - start));
    std::vector<Completion> out;
    bool member = start > 0 && source[start - 1] == '.';
    if (member) {
        std::string probe = std::string(source.substr(0, start)) + "zz" + std::string(source.substr(offset));
        auto analyzed = analyzeSource(probe, context);
        const Expr* target = nullptr;
        walkProgram(analyzed.parsed.program, [&](const Expr& e) {
            if (e.kind == ExprKind::member && e.text == "zz" && e.range.end.offset == start + 2) target = &e;
        });
        if (target && target->child(0)) {
            auto type = analyzed.analysis.types.find(target->child(0));
            if (type != analyzed.analysis.types.end()) membersOf(type->second, context, analyzed.analysis, out);
        }
    } else {
        auto analyzed = analyzeSource(source, context);
        std::set<std::string> seen;
        for (const auto& s : analyzed.analysis.symbols)
            if (visible(analyzed.analysis, s, start) && seen.insert(s.name).second) out.push_back({s.name, symbolKind(s.kind), s.type ? s.type->describe() : "", s.name, s.doc});
        for (const auto& [name, g] : context.registry.globals())
            if (seen.insert(name).second) out.push_back({name, CompletionKind::variable, g.type ? g.type->describe() : "", name, g.doc});
        for (const auto& [name, f] : context.registry.functions())
            if (seen.insert(name).second) out.push_back({name, CompletionKind::function, describeMethod(f), name + "(", f.doc});
        for (const auto& [name, c] : context.registry.classes())
            if (c.creatable && seen.insert(name).second) out.push_back({name, CompletionKind::type, "class " + name, name + "(", c.doc});
        for (const auto& [name, items] : context.registry.enums())
            if (seen.insert(name).second) out.push_back({name, CompletionKind::type, "enum " + name, name, ""});
        for (const auto& [name, g] : context.options.extraGlobals)
            if (seen.insert(name).second) out.push_back({name, CompletionKind::variable, g ? g->describe() : "", name, ""});
        for (const auto& k : keywords()) out.push_back({k, CompletionKind::keyword, tr("parola chiave"), k, ""});
        for (const auto& s : snippets()) out.push_back({s.trigger, CompletionKind::snippet, s.detail, s.body, s.body});
    }
    std::string lower = prefix;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::vector<Completion> filtered;
    for (auto& c : out) {
        std::string label = c.label;
        std::transform(label.begin(), label.end(), label.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (label.rfind(lower, 0) == 0) filtered.push_back(std::move(c));
    }
    std::stable_sort(filtered.begin(), filtered.end(), [&](const Completion& a, const Completion& b) {
        bool ea = a.label.rfind(prefix, 0) == 0, eb = b.label.rfind(prefix, 0) == 0;
        if (ea != eb) return ea;
        if ((a.kind == CompletionKind::snippet) != (b.kind == CompletionKind::snippet)) return b.kind == CompletionKind::snippet;
        return a.label < b.label;
    });
    return filtered;
}

std::optional<Hover> hover(std::string_view source, std::uint32_t offset, const Context& context) {
    auto analyzed = analyzeSource(source, context);
    const Analysis& a = analyzed.analysis;
    if (auto id = symbolAt(a, offset)) {
        const Symbol& s = a.symbols[static_cast<std::size_t>(*id)];
        Range range = s.range;
        for (const auto& ref : a.references)
            if (ref.symbol == *id && ref.range.start.offset <= offset && offset <= ref.range.end.offset) range = ref.range;
        return Hover{range, symbolTitle(s), s.doc};
    }
    std::optional<Hover> found;
    walkProgram(analyzed.parsed.program, [&](const Expr& e) {
        if (found || e.kind != ExprKind::member || !e.child(0)) return;
        std::uint32_t nameStart = e.range.end.offset >= e.text.size() ? e.range.end.offset - static_cast<std::uint32_t>(e.text.size()) : 0;
        if (offset < nameStart || offset > e.range.end.offset) return;
        auto type = a.types.find(e.child(0));
        if (type == a.types.end()) return;
        TypePtr owner = unwrapOptional(type->second);
        if (!owner) return;
        Range range{{nameStart, e.range.end.line, e.range.end.column - static_cast<std::uint32_t>(e.text.size())}, e.range.end};
        if (owner->kind == Type::Kind::host) {
            if (const HostMember* p = context.registry.property(owner->name, e.text)) found = Hover{range, p->name + ": " + (p->type ? p->type->describe() : "") + (p->readOnly ? " (sola lettura)" : ""), p->doc};
            else if (const HostMethod* m = context.registry.method(owner->name, e.text)) found = Hover{range, describeMethod(*m), m->doc};
            else if (const HostEvent* ev = context.registry.event(owner->name, e.text)) {
                std::string title = "evento " + ev->name + "(";
                for (std::size_t i = 0; i < ev->parameterNames.size(); ++i) title += (i ? ", " : "") + ev->parameterNames[i];
                found = Hover{range, title + ")", ev->doc.empty() ? tr("Usa «on oggetto.{0}(…) { }».", ev->name) : ev->doc};
            }
        } else if (isClassReference(owner)) {
            if (const HostClass* c = context.registry.findClass(classReferenceName(owner))) {
                if (auto st = c->statics.find(e.text); st != c->statics.end()) found = Hover{range, describeMethod(st->second), st->second.doc};
                else if (auto k = c->constants.find(e.text); k != c->constants.end()) found = Hover{range, k->first + ": " + (k->second.type ? k->second.type->describe() : ""), k->second.doc};
            }
        } else if (auto t = builtinMember(owner, e.text)) {
            found = Hover{range, e.text + ": " + t->describe(), ""};
        }
    });
    if (found) return found;
    auto lexed = lex(source);
    for (const auto& t : lexed.tokens) {
        if (t.kind != TokenKind::identifier || offset < t.range.start.offset || offset > t.range.end.offset) continue;
        if (const HostGlobal* g = context.registry.findGlobal(t.lexeme)) return Hover{t.range, t.lexeme + ": " + (g->type ? g->type->describe() : ""), g->doc};
        if (const HostMethod* f = context.registry.findFunction(t.lexeme)) return Hover{t.range, describeMethod(*f), f->doc};
        if (const HostClass* c = context.registry.findClass(t.lexeme)) return Hover{t.range, "class " + c->name, c->doc};
        if (context.registry.findEnum(t.lexeme)) return Hover{t.range, "enum " + t.lexeme, ""};
    }
    return std::nullopt;
}

std::optional<Range> definition(std::string_view source, std::uint32_t offset, const Context& context) {
    auto analyzed = analyzeSource(source, context);
    auto id = symbolAt(analyzed.analysis, offset);
    if (!id) return std::nullopt;
    const Symbol& s = analyzed.analysis.symbols[static_cast<std::size_t>(*id)];
    if (s.external) return std::nullopt;
    return s.range;
}

std::vector<Range> references(std::string_view source, std::uint32_t offset, const Context& context) {
    auto analyzed = analyzeSource(source, context);
    auto id = symbolAt(analyzed.analysis, offset);
    std::vector<Range> out;
    if (!id) return out;
    const Symbol& s = analyzed.analysis.symbols[static_cast<std::size_t>(*id)];
    if (!s.external) out.push_back(s.range);
    for (const auto& ref : analyzed.analysis.references)
        if (ref.symbol == *id) out.push_back(ref.range);
    std::sort(out.begin(), out.end(), [](const Range& a, const Range& b) { return a.start.offset < b.start.offset; });
    out.erase(std::unique(out.begin(), out.end(), [](const Range& a, const Range& b) { return a.start.offset == b.start.offset; }), out.end());
    return out;
}

RenameResult rename(std::string_view source, std::uint32_t offset, const std::string& name, const Context& context) {
    RenameResult result;
    bool valid = !name.empty() && !std::isdigit(static_cast<unsigned char>(name[0])) && std::all_of(name.begin(), name.end(), identifierCharacter);
    if (!valid) {
        result.error = tr("«{0}» non è un nome valido", name);
        return result;
    }
    if (isKeyword(name)) {
        result.error = tr("«{0}» è una parola chiave di Lim", name);
        return result;
    }
    auto analyzed = analyzeSource(source, context);
    auto id = symbolAt(analyzed.analysis, offset);
    if (!id) {
        result.error = tr("Qui non c’è un nome da rinominare");
        return result;
    }
    const Symbol& s = analyzed.analysis.symbols[static_cast<std::size_t>(*id)];
    if (s.external) {
        result.error = tr("«{0}» appartiene al motore e non si può rinominare", s.name);
        return result;
    }
    for (const auto& other : analyzed.analysis.symbols)
        if (other.id != s.id && other.name == name && !other.external) {
            result.error = tr("Esiste già «{0}»: scegli un altro nome", name);
            return result;
        }
    if (context.registry.findGlobal(name) || context.registry.findFunction(name) || context.registry.findClass(name)) {
        result.error = tr("«{0}» nasconderebbe un nome del motore", name);
        return result;
    }
    for (const auto& range : references(source, offset, context)) {
        std::string_view current = source.substr(range.start.offset, range.end.offset - range.start.offset);
        if (current != s.name) continue;
        result.edits.push_back({range, name});
    }
    return result;
}

std::string applyEdits(std::string_view source, std::vector<TextEdit> edits) {
    std::sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) { return a.range.start.offset > b.range.start.offset; });
    std::string text(source);
    for (const auto& e : edits) {
        if (e.range.start.offset > text.size() || e.range.end.offset > text.size() || e.range.end.offset < e.range.start.offset) continue;
        text.replace(e.range.start.offset, e.range.end.offset - e.range.start.offset, e.replacement);
    }
    return text;
}

}
