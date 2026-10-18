#include "Analyzer.hpp"

#include "Localization/Localization.hpp"

#include <algorithm>
#include <set>

namespace lim {

using ld::tr;

TypePtr classReference(const std::string& name) { return Type::host("@" + name); }
bool isClassReference(const TypePtr& type) { return type && type->kind == Type::Kind::host && !type->name.empty() && type->name[0] == '@'; }
std::string classReferenceName(const TypePtr& type) { return isClassReference(type) ? type->name.substr(1) : std::string(); }

namespace {

TypePtr userClassReference(const std::string& name) { return Type::user("@" + name); }
bool isUserClassReference(const TypePtr& type) { return type && type->kind == Type::Kind::user && !type->name.empty() && type->name[0] == '@'; }
TypePtr enumReference(const std::string& name) { return Type::enumeration("@" + name); }
bool isEnumReference(const TypePtr& type) { return type && type->kind == Type::Kind::enumeration && !type->name.empty() && type->name[0] == '@'; }

std::size_t editDistance(const std::string& a, const std::string& b) {
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) row[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t previous = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            std::size_t saved = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, previous + (std::tolower(a[i - 1]) == std::tolower(b[j - 1]) ? 0 : 1)});
            previous = saved;
        }
    }
    return row[b.size()];
}

}

TypePtr builtinMember(const TypePtr& owner, const std::string& name) {
    if (!owner) return nullptr;
    auto N = Type::number(), I = Type::integer(), T = Type::text(), B = Type::boolean(), A = Type::any();
    switch (owner->kind) {
    case Type::Kind::list: {
        TypePtr e = owner->arguments[0];
        if (name == "count") return I;
        if (name == "isEmpty") return B;
        if (name == "first" || name == "last") return Type::optional(e);
        if (name == "append") return Type::function({e}, Type::none(), false, 1, {"elemento"});
        if (name == "insert") return Type::function({e, I}, Type::none(), false, 2, {"elemento", "at"});
        if (name == "remove") return Type::function({I}, e, false, 1, {"at"});
        if (name == "removeLast") return Type::function({}, Type::optional(e));
        if (name == "clear") return Type::function({}, Type::none());
        if (name == "contains") return Type::function({e}, B, false, 1, {"elemento"});
        if (name == "indexOf") return Type::function({e}, Type::optional(I), false, 1, {"elemento"});
        if (name == "map") return Type::function({Type::function({e}, A)}, Type::list(A), false, 1, {"trasforma"});
        if (name == "filter") return Type::function({Type::function({e}, B)}, owner, false, 1, {"condizione"});
        if (name == "forEach") return Type::function({Type::function({e}, A)}, Type::none(), false, 1, {"azione"});
        if (name == "sorted") return Type::function({Type::function({e, e}, B)}, owner, false, 0, {"ordine"});
        if (name == "reversed") return Type::function({}, owner);
        if (name == "join") return Type::function({T}, T, false, 0, {"separatore"});
        if (name == "copy") return Type::function({}, owner);
        return nullptr;
    }
    case Type::Kind::map: {
        TypePtr k = owner->arguments[0], v = owner->arguments[1];
        if (name == "count") return I;
        if (name == "isEmpty") return B;
        if (name == "keys") return Type::list(k);
        if (name == "values") return Type::list(v);
        if (name == "contains") return Type::function({k}, B, false, 1, {"chiave"});
        if (name == "remove") return Type::function({k}, Type::optional(v), false, 1, {"chiave"});
        if (name == "get") return Type::function({k, v}, Type::optional(v), false, 1, {"chiave", "altrimenti"});
        if (name == "clear") return Type::function({}, Type::none());
        if (name == "copy") return Type::function({}, owner);
        return nullptr;
    }
    case Type::Kind::text: {
        if (name == "count") return I;
        if (name == "isEmpty") return B;
        if (name == "upper" || name == "lower" || name == "trimmed") return Type::function({}, T);
        if (name == "contains" || name == "startsWith" || name == "endsWith") return Type::function({T}, B, false, 1, {"testo"});
        if (name == "split") return Type::function({T}, Type::list(T), false, 1, {"separatore"});
        if (name == "replace") return Type::function({T, T}, T, false, 2, {"cerca", "con"});
        if (name == "find") return Type::function({T}, Type::optional(I), false, 1, {"testo"});
        if (name == "slice") return Type::function({I, I}, T, false, 1, {"da", "a"});
        if (name == "repeat") return Type::function({I}, T, false, 1, {"volte"});
        return nullptr;
    }
    case Type::Kind::task: {
        if (name == "cancel") return Type::function({}, Type::none());
        if (name == "done") return B;
        return nullptr;
    }
    case Type::Kind::enumeration: {
        if (name == "name" || name == "type") return T;
        return nullptr;
    }
    default: return nullptr;
    }
}

std::vector<std::string> builtinMembers(const TypePtr& owner) {
    if (!owner) return {};
    switch (owner->kind) {
    case Type::Kind::list: return {"count", "isEmpty", "first", "last", "append", "insert", "remove", "removeLast", "clear", "contains", "indexOf", "map", "filter", "forEach", "sorted", "reversed", "join", "copy"};
    case Type::Kind::map: return {"count", "isEmpty", "keys", "values", "contains", "remove", "get", "clear", "copy"};
    case Type::Kind::text: return {"count", "isEmpty", "upper", "lower", "trimmed", "contains", "startsWith", "endsWith", "split", "replace", "find", "slice", "repeat"};
    case Type::Kind::task: return {"cancel", "done"};
    case Type::Kind::enumeration: return {"name", "type"};
    default: return {};
    }
}

bool assignable(const TypePtr& from, const TypePtr& to, const TypeRegistry& registry) {
    if (!from || !to) return true;
    if (from->isAny() || to->isAny()) return true;
    if (to->kind == Type::Kind::optional) {
        if (from->kind == Type::Kind::none) return true;
        if (from->kind == Type::Kind::optional) return assignable(from->arguments[0], to->arguments[0], registry);
        return assignable(from, to->arguments[0], registry);
    }
    if (from->kind == Type::Kind::optional) return false;
    if (from->kind == Type::Kind::integer && to->kind == Type::Kind::number) return true;
    if (from->kind != to->kind) return false;
    switch (from->kind) {
    case Type::Kind::list: return assignable(from->arguments[0], to->arguments[0], registry);
    case Type::Kind::map: return assignable(from->arguments[0], to->arguments[0], registry) && assignable(from->arguments[1], to->arguments[1], registry);
    case Type::Kind::host: return from->name == to->name || registry.isA(from->name, to->name);
    case Type::Kind::user:
    case Type::Kind::enumeration: return from->name == to->name;
    case Type::Kind::function: return from->arguments.size() <= to->arguments.size() || to->arguments.empty();
    case Type::Kind::task: return assignable(from->result, to->result, registry);
    default: return true;
    }
}

namespace {

class Analyzer {
public:
    Analyzer(const TypeRegistry& registry, const AnalyzeOptions& options) : registry(registry), options(options) {}

    Analysis run(const Program& program) {
        pushScope({});
        for (const auto& [name, type] : options.extraGlobals) declare(name, SymbolKind::global, type, {}, false, true);
        hoist(program.statements);
        for (const auto& statement : program.statements) statementCheck(*statement, true);
        popScope();
        return std::move(analysis);
    }

private:
    const TypeRegistry& registry;
    const AnalyzeOptions& options;
    Analysis analysis;

    struct Scope {
        int info = -1;
        std::map<std::string, int> names;
        std::map<std::string, TypePtr> narrowed;
    };
    std::vector<Scope> scopes;

    struct FunctionContext {
        bool async = false;
        TypePtr returns;
        bool inferReturn = false;
        std::vector<TypePtr> returned;
        std::string className;
        bool initializer = false;
    };
    std::vector<FunctionContext> functions;
    int loops = 0;

    void error(const std::string& message, Range range, std::string help = {}, std::string expected = {}, std::string actual = {}, std::string code = "tipo") {
        Diagnostic d;
        d.code = std::move(code);
        d.message = message;
        d.file = options.file;
        d.range = range;
        d.help = std::move(help);
        d.expected = std::move(expected);
        d.actual = std::move(actual);
        analysis.diagnostics.push_back(std::move(d));
    }

    void deprecated(const std::string& name, const std::string& replacement, Range range) {
        warning(tr("«{0}» è deprecato", name) + (replacement.empty() ? std::string() : tr(": usa «{0}»", replacement)), range, {}, "deprecato");
    }

    void warning(const std::string& message, Range range, std::string help = {}, std::string code = "avviso") {
        Diagnostic d;
        d.severity = Severity::warning;
        d.code = code;
        d.message = message;
        d.file = options.file;
        d.range = range;
        d.help = std::move(help);
        analysis.diagnostics.push_back(std::move(d));
    }

    void pushScope(Range range) {
        ScopeInfo info;
        info.range = range;
        info.parent = scopes.empty() ? -1 : scopes.back().info;
        analysis.scopes.push_back(info);
        scopes.push_back({static_cast<int>(analysis.scopes.size()) - 1, {}, {}});
    }

    void popScope() { scopes.pop_back(); }

    int declare(const std::string& name, SymbolKind kind, TypePtr type, Range range, bool mutable_, bool external = false, std::string doc = {}) {
        if (name.empty()) return -1;
        auto& scope = scopes.back();
        if (auto existing = scope.names.find(name); existing != scope.names.end() && !external) {
            const Symbol& old = analysis.symbols[static_cast<std::size_t>(existing->second)];
            if (!old.external) {
                error(tr("«{0}» è già dichiarato in questo blocco", name), range, tr("Scegli un nome diverso oppure usa l’assegnazione senza «let»/«var»."), {}, {}, "duplicato");
                return existing->second;
            }
        }
        Symbol symbol;
        symbol.id = static_cast<int>(analysis.symbols.size());
        symbol.name = name;
        symbol.kind = kind;
        symbol.type = type ? type : Type::any();
        symbol.range = range;
        symbol.mutable_ = mutable_;
        symbol.external = external;
        symbol.doc = std::move(doc);
        if (!functions.empty()) symbol.container = functions.back().className;
        analysis.symbols.push_back(symbol);
        scope.names[name] = symbol.id;
        analysis.scopes[static_cast<std::size_t>(scope.info)].symbols.push_back(symbol.id);
        if (!external) analysis.references.push_back({range, symbol.id});
        return symbol.id;
    }

    std::optional<int> lookup(const std::string& name) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            if (auto found = it->names.find(name); found != it->names.end()) return found->second;
        return std::nullopt;
    }

    std::optional<TypePtr> narrowedType(const std::string& name) {
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
            if (auto found = it->narrowed.find(name); found != it->narrowed.end()) return found->second;
            if (it->names.count(name)) return std::nullopt;
        }
        return std::nullopt;
    }

    TypePtr resolveType(const TypeRef& ref) {
        if (!ref) return Type::any();
        switch (ref->kind) {
        case TypeExpr::Kind::optional: return Type::optional(resolveType(ref->arguments[0]));
        case TypeExpr::Kind::list: return Type::list(resolveType(ref->arguments[0]));
        case TypeExpr::Kind::map: return Type::map(resolveType(ref->arguments[0]), resolveType(ref->arguments[1]));
        case TypeExpr::Kind::function: {
            std::vector<TypePtr> parameters;
            for (const auto& a : ref->arguments) parameters.push_back(resolveType(a));
            return Type::function(parameters, ref->result ? resolveType(ref->result) : Type::none());
        }
        case TypeExpr::Kind::named: break;
        }
        const std::string& n = ref->name;
        if (n == "Any") return Type::any();
        if (n == "None") return Type::none();
        if (n == "Bool") return Type::boolean();
        if (n == "Integer") return Type::integer();
        if (n == "Number") return Type::number();
        if (n == "Text") return Type::text();
        if (n == "Task") return Type::task(Type::any());
        if (analysis.classes.count(n)) return Type::user(n);
        if (analysis.enums.count(n)) return Type::enumeration(n);
        if (registry.findClass(n)) return Type::host(n);
        if (registry.findEnum(n)) return Type::enumeration(n);
        std::string suggestion = suggest(n, {"Any", "None", "Bool", "Integer", "Number", "Text"});
        error(tr("Tipo sconosciuto «{0}»", n), ref->range, suggestion.empty() ? tr("I tipi di base sono Bool, Integer, Number, Text e Any.") : tr("Forse intendevi «{0}»?", suggestion));
        return Type::error();
    }

    std::string suggest(const std::string& name, std::vector<std::string> extra = {}) {
        std::string best;
        std::size_t bestScore = std::max<std::size_t>(2, name.size() / 3) + 1;
        auto consider = [&](const std::string& candidate) {
            std::size_t score = editDistance(name, candidate);
            if (score < bestScore) { bestScore = score; best = candidate; }
        };
        for (auto& e : extra) consider(e);
        for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
            for (const auto& [n, id] : it->names) consider(n);
        for (const auto& [n, g] : registry.globals()) consider(n);
        for (const auto& [n, f] : registry.functions()) consider(n);
        for (const auto& [n, c] : registry.classes())
            if (c.creatable || c.constructor) consider(n);
        return best;
    }

    void hoist(const std::vector<StmtPtr>& statements) {
        for (const auto& s : statements) {
            if (s->kind == StmtKind::classDecl) {
                UserClass c;
                c.name = s->name;
                c.range = s->nameRange;
                analysis.classes[s->name] = c;
                declare(s->name, SymbolKind::classType, userClassReference(s->name), s->nameRange, false);
            }
            if (s->kind == StmtKind::enumDecl) {
                std::vector<std::string> cases;
                for (const auto& [name, range] : s->enumCases) {
                    if (std::find(cases.begin(), cases.end(), name) != cases.end()) error(tr("Il caso «{0}» è ripetuto", name), range);
                    cases.push_back(name);
                }
                analysis.enums[s->name] = cases;
                declare(s->name, SymbolKind::enumType, enumReference(s->name), s->nameRange, false);
            }
        }
        for (const auto& s : statements) {
            if (s->kind == StmtKind::classDecl) defineClass(*s);
            if (s->kind == StmtKind::function) {
                auto type = functionType(*s->function);
                declare(s->name, SymbolKind::function, type, s->nameRange, false);
                if (s->exported || scopes.size() == 1) exportName(s->name, type);
            }
            if (s->kind == StmtKind::import) {
                auto module = options.resolveModule ? options.resolveModule(s->name) : std::nullopt;
                analysis.imports.push_back(s->name);
                if (!module) {
                    error(tr("Modulo «{0}» non trovato", s->name), s->nameRange, tr("Crea un ModuleScript con questo nome in ReplicatedStorage, oppure controlla il nome."), {}, {}, "modulo");
                    declare(s->name, SymbolKind::module, Type::any(), s->nameRange, false);
                } else {
                    for (const auto& [name, fields] : module->classFields) {
                        UserClass c;
                        c.name = name;
                        c.fields = fields;
                        for (auto& f : fields) c.mutableFields[f.first] = true;
                        analysis.classes[name] = c;
                    }
                    for (const auto& [name, cases] : module->enums) analysis.enums[name] = cases;
                    declare(s->name, SymbolKind::module, Type::module(s->name), s->nameRange, false);
                    modules[s->name] = *module;
                }
            }
        }
    }

    std::map<std::string, ModuleInterface> modules;

    void exportName(const std::string& name, TypePtr type) {
        if (scopes.size() == 1) analysis.interface.exports[name] = std::move(type);
    }

    TypePtr functionType(const FunctionDecl& f) {
        std::vector<TypePtr> parameters;
        std::vector<std::string> names;
        int minimum = 0;
        for (const auto& p : f.parameters) {
            parameters.push_back(p.type ? resolveType(p.type) : Type::any());
            names.push_back(p.name);
            if (!p.fallback) minimum = static_cast<int>(parameters.size());
        }
        TypePtr result = f.returns ? resolveType(f.returns) : Type::any();
        return Type::function(parameters, result, f.async, minimum, names);
    }

    void defineClass(const Stmt& s) {
        UserClass& c = analysis.classes[s.name];
        std::set<std::string> seen;
        for (const auto& field : s.fields) {
            if (!seen.insert(field.name).second) error(tr("Il campo «{0}» è ripetuto", field.name), field.range);
            TypePtr type = field.type ? resolveType(field.type) : Type::any();
            c.fields.emplace_back(field.name, type);
            c.mutableFields[field.name] = field.mutable_;
        }
        for (const auto& method : s.methods) {
            if (!seen.insert(method->name).second) error(tr("Il membro «{0}» è ripetuto", method->name), method->nameRange);
            auto type = functionType(*method);
            if (method->isInit) c.initializer = type;
            else c.methods[method->name] = type;
        }
        analysis.interface.classFields[s.name] = c.fields;
        if (scopes.size() == 1) analysis.interface.exports[s.name] = userClassReference(s.name);
    }

    void block(const Block& b, Range range) {
        pushScope(range);
        hoist(b.statements);
        for (const auto& s : b.statements) statementCheck(*s, false);
        popScope();
    }

    void blockWithNarrowing(const Block& b, const std::map<std::string, TypePtr>& narrowed) {
        pushScope(b.range);
        scopes.back().narrowed = narrowed;
        hoist(b.statements);
        for (const auto& s : b.statements) statementCheck(*s, false);
        popScope();
    }

    bool canAwait() const {
        if (functions.empty()) return options.topLevelAsync;
        return functions.back().async;
    }

    void statementCheck(const Stmt& s, bool topLevel) {
        switch (s.kind) {
        case StmtKind::declaration: {
            TypePtr declared = s.type ? resolveType(s.type) : nullptr;
            TypePtr value = s.value ? expression(*s.value, declared) : nullptr;
            if (declared && value && !assignable(value, declared, registry)) {
                error(tr("Tipo non compatibile per «{0}»", s.name), s.value->range, typeHelp(value, declared), declared->describe(), value->describe());
            }
            TypePtr type = declared ? declared : value ? value : Type::any();
            if (!declared && value && value->kind == Type::Kind::none && s.mutable_) type = Type::any();
            if (!declared && value && value->kind == Type::Kind::none && !s.mutable_) warning(tr("«{0}» vale sempre none", s.name), s.nameRange);
            int symbol = declare(s.name, s.mutable_ ? SymbolKind::variable : SymbolKind::constant, type, s.nameRange, s.mutable_);
            if (symbol >= 0 && !declared && s.mutable_) analysis.symbols[static_cast<std::size_t>(symbol)].inferred = true;
            if (topLevel) exportName(s.name, type);
            break;
        }
        case StmtKind::function: functionBody(*s.function, {}); break;
        case StmtKind::classDecl: classBody(s); break;
        case StmtKind::enumDecl:
            if (topLevel) analysis.interface.enums[s.name] = analysis.enums[s.name];
            if (topLevel) analysis.interface.exports[s.name] = enumReference(s.name);
            break;
        case StmtKind::import: break;
        case StmtKind::ifStmt: {
            auto condition = expression(*s.value, Type::boolean());
            expectCondition(condition, s.value->range);
            std::map<std::string, TypePtr> positive, negative;
            narrowing(*s.value, positive, negative);
            blockWithNarrowing(*s.body, positive);
            if (s.otherwise) blockWithNarrowing(*s.otherwise, negative);
            break;
        }
        case StmtKind::whileStmt: {
            auto condition = expression(*s.value, Type::boolean());
            expectCondition(condition, s.value->range);
            ++loops;
            block(*s.body, s.body->range);
            --loops;
            break;
        }
        case StmtKind::forStmt: {
            auto iterable = expression(*s.value, nullptr);
            TypePtr first = Type::any(), second = Type::any();
            TypePtr base = unwrapOptional(iterable);
            if (iterable && iterable->kind == Type::Kind::optional) error(tr("Non si può scorrere un valore che può essere none"), s.value->range, tr("Controlla prima con «if lista != none»."));
            if (base) {
                switch (base->kind) {
                case Type::Kind::list: first = base->arguments[0]; if (s.names.size() == 2) { second = first; first = Type::integer(); } break;
                case Type::Kind::map: first = base->arguments[0]; second = base->arguments[1]; break;
                case Type::Kind::text: first = Type::text(); break;
                case Type::Kind::any:
                case Type::Kind::error: break;
                default: error(tr("«{0}» non si può scorrere con for", base->describe()), s.value->range, tr("Si possono scorrere liste, mappe, intervalli come 1…10 e testi."));
                }
            }
            ++loops;
            pushScope(s.body->range);
            declare(s.names[0], SymbolKind::constant, first, s.nameRanges[0], false);
            if (s.names.size() > 1) declare(s.names[1], SymbolKind::constant, second, s.nameRanges[1], false);
            hoist(s.body->statements);
            for (const auto& st : s.body->statements) statementCheck(*st, false);
            popScope();
            --loops;
            break;
        }
        case StmtKind::matchStmt: {
            auto subject = expression(*s.value, nullptr);
            bool sawElse = false;
            std::set<std::string> covered;
            for (const auto& c : s.cases) {
                if (c.otherwise) {
                    if (sawElse) error(tr("«else» può comparire una sola volta in match"), c.range);
                    sawElse = true;
                }
                for (const auto& pattern : c.patterns) {
                    auto type = expression(*pattern, subject);
                    if (pattern->kind == ExprKind::shorthand) covered.insert(pattern->text);
                    if (subject && type && !assignable(type, subject, registry) && !assignable(subject, type, registry))
                        error(tr("Questo caso non può corrispondere al valore"), pattern->range, {}, subject->describe(), type->describe());
                }
                block(*c.body, c.body->range);
            }
            if (!sawElse && subject && subject->kind == Type::Kind::enumeration) {
                auto it = analysis.enums.find(subject->name);
                const std::vector<std::string>* cases = it != analysis.enums.end() ? &it->second : registry.findEnum(subject->name);
                if (cases) {
                    std::vector<std::string> missing;
                    for (const auto& c : *cases)
                        if (!covered.count(c)) missing.push_back(c);
                    if (!missing.empty()) {
                        std::string list;
                        for (std::size_t i = 0; i < missing.size() && i < 5; ++i) list += (i ? ", ." : ".") + missing[i];
                        warning(tr("match non copre tutti i casi di {0}", subject->name), s.value->range, tr("Mancano {0}; aggiungili o usa «else».", list));
                    }
                }
            }
            break;
        }
        case StmtKind::returnStmt: {
            if (functions.empty()) {
                if (s.value) expression(*s.value, nullptr);
                break;
            }
            std::size_t owner = functions.size() - 1;
            TypePtr expectedReturn = functions[owner].inferReturn ? nullptr : functions[owner].returns;
            TypePtr value = s.value ? expression(*s.value, expectedReturn) : Type::none();
            auto& f = functions[owner];
            if (f.inferReturn) f.returned.push_back(value);
            else if (f.returns && !assignable(value, f.returns, registry) && !(f.returns->kind == Type::Kind::none && !s.value))
                error(tr("Valore restituito non compatibile"), s.value ? s.value->range : s.range, typeHelp(value, f.returns), f.returns->describe(), value->describe());
            break;
        }
        case StmtKind::breakStmt:
        case StmtKind::continueStmt:
            if (loops == 0) error(std::string("«") + tr("{0}» è valido solo dentro un ciclo", (s.kind == StmtKind::breakStmt ? "break" : "continue")), s.range);
            break;
        case StmtKind::expression: {
            auto type = expression(*s.value, nullptr);
            if (type && type->kind == Type::Kind::task && s.value->kind == ExprKind::call && canAwait())
                warning(tr("Questa funzione è async: senza «await» parte in parallelo"), s.value->range, tr("Scrivi «await» davanti alla chiamata per aspettarne la fine."));
            break;
        }
        case StmtKind::assign: assignment(s); break;
        case StmtKind::on: onStatement(s); break;
        case StmtKind::block: block(*s.body, s.body->range); break;
        }
    }

    void expectCondition(const TypePtr& type, Range range) {
        if (type && !type->isAny() && type->kind != Type::Kind::boolean)
            error(tr("La condizione deve essere vera o falsa"), range, type->kind == Type::Kind::optional ? tr("Per controllare un valore opzionale scrivi «x != none».") : "", "Bool", type->describe());
    }

    void narrowing(const Expr& condition, std::map<std::string, TypePtr>& positive, std::map<std::string, TypePtr>& negative) {
        if (condition.kind == ExprKind::binary && (condition.op == "!=" || condition.op == "==")) {
            const Expr* left = condition.child(0);
            const Expr* right = condition.child(1);
            const Expr* name = nullptr;
            if (left && left->kind == ExprKind::identifier && right && right->kind == ExprKind::none) name = left;
            if (right && right->kind == ExprKind::identifier && left && left->kind == ExprKind::none) name = right;
            if (name) {
                auto id = lookup(name->text);
                if (!id) return;
                TypePtr current = narrowedType(name->text).value_or(analysis.symbols[static_cast<std::size_t>(*id)].type);
                TypePtr inner = unwrapOptional(current);
                if (condition.op == "!=") positive[name->text] = inner;
                else negative[name->text] = inner;
            }
        }
        if (condition.kind == ExprKind::is && condition.child(0) && condition.child(0)->kind == ExprKind::identifier && condition.type) {
            if (lookup(condition.child(0)->text)) positive[condition.child(0)->text] = resolveType(condition.type);
        }
        if (condition.kind == ExprKind::binary && condition.op == "&&") {
            std::map<std::string, TypePtr> ignore;
            narrowing(*condition.child(0), positive, ignore);
            narrowing(*condition.child(1), positive, ignore);
        }
    }

    std::string typeHelp(const TypePtr& from, const TypePtr& to) {
        if (!from || !to) return {};
        if (from->kind == Type::Kind::text && to->numeric()) return tr("Per convertire un testo in numero usa Number(\"…\").");
        if (from->numeric() && to->kind == Type::Kind::text) return tr("Per ottenere un testo usa l’interpolazione: \"\\(valore)\".");
        if (from->kind == Type::Kind::number && to->kind == Type::Kind::integer) return tr("Per ottenere un intero usa Integer(valore) oppure Math.round(valore).");
        if (from->kind == Type::Kind::optional) return tr("Il valore può essere none: controllalo prima con «if x != none» oppure usa «??».");
        return {};
    }

    void functionBody(const FunctionDecl& f, const std::string& className) {
        FunctionContext context;
        context.async = f.async;
        context.inferReturn = !f.returns;
        context.returns = f.returns ? resolveType(f.returns) : nullptr;
        context.className = className;
        context.initializer = f.isInit;
        functions.push_back(context);
        int savedLoops = loops;
        loops = 0;
        pushScope(f.body ? f.body->range : Range{});
        for (const auto& p : f.parameters) {
            TypePtr type = p.type ? resolveType(p.type) : Type::any();
            if (p.fallback) {
                auto fallback = expression(*p.fallback, type);
                if (!assignable(fallback, type, registry)) error(tr("Valore predefinito non compatibile"), p.fallback->range, {}, type->describe(), fallback->describe());
            }
            declare(p.name, SymbolKind::parameter, type, p.range, false);
        }
        if (f.body) {
            hoist(f.body->statements);
            for (const auto& s : f.body->statements) statementCheck(*s, false);
        }
        popScope();
        loops = savedLoops;
        functions.pop_back();
    }

    TypePtr lambdaBody(const Expr& e, const TypePtr& expected) {
        FunctionContext context;
        context.async = e.async;
        context.inferReturn = !e.returns;
        context.returns = e.returns ? resolveType(e.returns) : nullptr;
        if (!functions.empty()) context.className = functions.back().className;
        functions.push_back(context);
        int savedLoops = loops;
        loops = 0;
        pushScope(e.body ? e.body->range : e.range);
        std::vector<TypePtr> parameters;
        std::vector<std::string> names;
        for (std::size_t i = 0; i < e.parameters.size(); ++i) {
            const auto& p = e.parameters[i];
            TypePtr type = p.type ? resolveType(p.type) : Type::any();
            if (!p.type && expected && expected->kind == Type::Kind::function && i < expected->arguments.size()) type = expected->arguments[i];
            declare(p.name, SymbolKind::parameter, type, p.range, false);
            parameters.push_back(type);
            names.push_back(p.name);
        }
        if (e.body) {
            hoist(e.body->statements);
            for (const auto& s : e.body->statements) statementCheck(*s, false);
        }
        TypePtr result = context.returns;
        auto& finished = functions.back();
        if (!result) result = finished.returned.empty() ? Type::none() : finished.returned.front();
        popScope();
        loops = savedLoops;
        functions.pop_back();
        return Type::function(parameters, result, e.async, static_cast<int>(parameters.size()), names);
    }

    void classBody(const Stmt& s) {
        const UserClass& c = analysis.classes[s.name];
        pushScope(s.range);
        for (const auto& [name, type] : c.fields) declare(name, SymbolKind::field, type, fieldRange(s, name), c.mutableFields.at(name));
        for (const auto& method : s.methods) {
            auto it = c.methods.find(method->name);
            declare(method->name, SymbolKind::method, it != c.methods.end() ? it->second : Type::any(), method->nameRange, false);
        }
        for (const auto& field : s.fields) {
            if (!field.initializer) continue;
            TypePtr declared = field.type ? resolveType(field.type) : nullptr;
            auto value = expression(*field.initializer, declared);
            if (declared && !assignable(value, declared, registry)) error(tr("Valore iniziale non compatibile per «{0}»", field.name), field.initializer->range, typeHelp(value, declared), declared->describe(), value->describe());
        }
        for (const auto& method : s.methods) functionBody(*method, s.name);
        popScope();
    }

    Range fieldRange(const Stmt& s, const std::string& name) {
        for (const auto& f : s.fields)
            if (f.name == name) return f.range;
        return s.nameRange;
    }

    void assignment(const Stmt& s) {
        const Expr& target = *s.target;
        TypePtr targetType;
        int widenable = -1;
        if (target.kind == ExprKind::identifier) {
            auto id = lookup(target.text);
            if (!id) {
                std::string hint = suggest(target.text);
                error(tr("Nome sconosciuto «{0}»", target.text), target.range, hint.empty() ? tr("Dichiara prima la variabile con «var {0} = …».", target.text) : tr("Forse intendevi «{0}»?", hint), {}, {}, "nome");
                targetType = Type::error();
            } else {
                const Symbol& symbol = analysis.symbols[static_cast<std::size_t>(*id)];
                analysis.references.push_back({target.range, symbol.id});
                analysis.resolved[&target] = symbol.id;
                if (symbol.kind == SymbolKind::constant) error(tr("«{0}» è una costante (let) e non si può modificare", symbol.name), target.range, tr("Dichiarala con «var» se deve cambiare."), {}, {}, "costante");
                else if (symbol.kind == SymbolKind::parameter) error(tr("Il parametro «{0}» non si può modificare", symbol.name), target.range, tr("Copialo in una variabile: var {0}2 = {1}", symbol.name, symbol.name), {}, {}, "costante");
                else if (symbol.kind == SymbolKind::field && !symbol.mutable_ && (functions.empty() || !isInit())) error(tr("Il campo «{0}» è una costante", symbol.name), target.range, {}, {}, {}, "costante");
                else if (symbol.kind == SymbolKind::function || symbol.kind == SymbolKind::classType || symbol.kind == SymbolKind::enumType || symbol.kind == SymbolKind::module || symbol.kind == SymbolKind::global || symbol.kind == SymbolKind::method)
                    error(tr("«{0}» non si può riassegnare", symbol.name), target.range, {}, {}, {}, "costante");
                targetType = symbol.type;
                if (symbol.inferred && symbol.kind == SymbolKind::variable && targetType->kind == Type::Kind::integer) widenable = symbol.id;
            }
            analysis.types[&target] = targetType;
        } else if (target.kind == ExprKind::member) {
            targetType = memberAccess(target, true);
        } else if (target.kind == ExprKind::index) {
            targetType = expression(target, nullptr);
        } else targetType = expression(target, nullptr);
        auto value = expression(*s.value, s.op == "=" ? targetType : nullptr);
        auto widenInferred = [&](const TypePtr& result) {
            if (widenable < 0 || !result || result->kind != Type::Kind::number) return false;
            analysis.symbols[static_cast<std::size_t>(widenable)].type = Type::number();
            return true;
        };
        if (s.op == "=") {
            if (widenInferred(value)) return;
            if (targetType && value && !assignable(value, targetType, registry))
                error(tr("Tipo non compatibile nell’assegnazione"), s.value->range, typeHelp(value, targetType), targetType->describe(), value->describe());
        } else {
            auto result = arithmetic(s.op.substr(0, 1), targetType, value, s.range);
            if (widenInferred(result)) return;
            if (targetType && result && !assignable(result, targetType, registry)) error(tr("Risultato non compatibile con «{0}»", describeTarget(target)), s.range, typeHelp(result, targetType), targetType->describe(), result->describe());
        }
    }

    bool isInit() const { return !functions.empty() && functions.back().initializer; }

    std::string describeTarget(const Expr& e) {
        if (e.kind == ExprKind::identifier) return e.text;
        if (e.kind == ExprKind::member) return e.text;
        return "destinazione";
    }

    void onStatement(const Stmt& s) {
        const Expr& target = *s.target;
        std::vector<TypePtr> parameters;
        if (target.kind == ExprKind::member) {
            auto ownerType = expression(*target.child(0), nullptr);
            TypePtr owner = unwrapOptional(ownerType);
            if (ownerType && ownerType->kind == Type::Kind::optional) error(tr("L’oggetto può essere none"), target.child(0)->range, tr("Controllalo prima con «if x != none»."));
            bool known = false;
            if (owner && owner->kind == Type::Kind::host && !isClassReference(owner)) {
                if (const HostEvent* event = registry.event(owner->name, target.text)) {
                    if (event->deprecated) deprecated(target.text, event->replacement, target.range);
                    parameters = event->parameters;
                    known = !event->variadic;
                    if (event->variadic)
                        while (parameters.size() < s.parameters.size()) parameters.push_back(Type::any());
                } else if (registry.subclassMember(owner->name, target.text) == MemberKind::event) {
                } else {
                    std::vector<std::string> names;
                    for (auto* e : registry.events(owner->name)) names.push_back(e->name);
                    std::string hint;
                    for (auto& n : names)
                        if (editDistance(n, target.text) <= 2) hint = n;
                    error(tr("«{0}» non è un evento di {1}", target.text, owner->name), target.range, hint.empty() ? (names.empty() ? "" : tr("Eventi disponibili: {0}", joinNames(names))) : tr("Forse intendevi «{0}»?", hint), {}, {}, "membro");
                }
            } else if (owner && !owner->isAny()) error(tr("Dopo «on» serve un evento di un oggetto"), target.range);
            if (!known) parameters.assign(s.parameters.size(), Type::any());
            analysis.types[&target] = Type::host("Event");
            if (known && s.parameters.size() > parameters.size())
                error(tr("L’evento «{0}» fornisce {1} valori", target.text, std::to_string(parameters.size())), s.parameters[parameters.size()].range);
        }
        FunctionContext context;
        context.async = true;
        context.inferReturn = true;
        if (!functions.empty()) context.className = functions.back().className;
        functions.push_back(context);
        int savedLoops = loops;
        loops = 0;
        pushScope(s.body->range);
        for (std::size_t i = 0; i < s.parameters.size(); ++i) {
            const auto& p = s.parameters[i];
            TypePtr type = p.type ? resolveType(p.type) : i < parameters.size() ? parameters[i] : Type::any();
            if (p.type && i < parameters.size() && !assignable(parameters[i], type, registry)) error(tr("Tipo del parametro non compatibile con l’evento"), p.range, {}, parameters[i]->describe(), type->describe());
            declare(p.name, SymbolKind::parameter, type, p.range, false);
        }
        hoist(s.body->statements);
        for (const auto& st : s.body->statements) statementCheck(*st, false);
        popScope();
        loops = savedLoops;
        functions.pop_back();
    }

    static std::string joinNames(const std::vector<std::string>& names) {
        std::string out;
        for (std::size_t i = 0; i < names.size() && i < 8; ++i) out += (i ? ", " : "") + names[i];
        return out;
    }

    void checkContext(ExecutionContext only, const std::string& what, Range range) {
        if (only == ExecutionContext::shared) return;
        if (options.context == ExecutionContext::shared || options.context == ExecutionContext::edit) return;
        if (only != options.context)
            error(tr("«{0}» è disponibile solo negli script {1}", what, std::string(contextName(only))), range,
                  only == ExecutionContext::server ? tr("Sposta questo codice in uno Script sul server e usa Network per comunicare.") : tr("Sposta questo codice in un LocalScript del giocatore."), {}, {}, "contesto");
    }

    TypePtr record(const Expr& e, TypePtr type) {
        analysis.types[&e] = type ? type : Type::any();
        return analysis.types[&e];
    }

    TypePtr expression(const Expr& e, const TypePtr& expected) {
        switch (e.kind) {
        case ExprKind::none: return record(e, Type::none());
        case ExprKind::boolean: return record(e, Type::boolean());
        case ExprKind::integer: return record(e, expected && expected->kind == Type::Kind::number ? Type::number() : Type::integer());
        case ExprKind::number: return record(e, Type::number());
        case ExprKind::text: return record(e, Type::text());
        case ExprKind::interpolation:
            for (const auto& c : e.children) expression(*c, nullptr);
            return record(e, Type::text());
        case ExprKind::identifier: return record(e, identifier(e));
        case ExprKind::self: {
            if (functions.empty() || functions.back().className.empty()) {
                error(tr("«self» è valido solo nei metodi di una classe"), e.range);
                return record(e, Type::error());
            }
            return record(e, Type::user(functions.back().className));
        }
        case ExprKind::shorthand: return record(e, shorthand(e, expected));
        case ExprKind::list: {
            TypePtr element = expected && unwrapOptional(expected)->kind == Type::Kind::list ? unwrapOptional(expected)->arguments[0] : nullptr;
            TypePtr common;
            for (const auto& c : e.children) {
                auto t = expression(*c, element);
                if (element && !assignable(t, element, registry)) error(tr("Elemento non compatibile con la lista"), c->range, {}, element->describe(), t->describe());
                if (!common) common = t;
                else if (!sameType(common, t)) common = common->numeric() && t->numeric() ? Type::number() : Type::any();
            }
            return record(e, Type::list(element ? element : common ? common : Type::any()));
        }
        case ExprKind::map: {
            TypePtr expectedMap = expected ? unwrapOptional(expected) : nullptr;
            TypePtr keyExpected = expectedMap && expectedMap->kind == Type::Kind::map ? expectedMap->arguments[0] : nullptr;
            TypePtr valueExpected = expectedMap && expectedMap->kind == Type::Kind::map ? expectedMap->arguments[1] : nullptr;
            TypePtr key, value;
            for (const auto& [k, v] : e.entries) {
                auto kt = expression(*k, keyExpected);
                auto vt = expression(*v, valueExpected);
                if (kt && !kt->isAny() && kt->kind != Type::Kind::text && !kt->numeric() && kt->kind != Type::Kind::boolean && kt->kind != Type::Kind::enumeration)
                    error(tr("Le chiavi di una mappa devono essere testo, numeri, Bool o casi di enum"), k->range);
                if (!key) key = kt;
                else if (!sameType(key, kt)) key = Type::any();
                if (!value) value = vt;
                else if (!sameType(value, vt)) value = value->numeric() && vt->numeric() ? Type::number() : Type::any();
            }
            return record(e, Type::map(keyExpected ? keyExpected : key ? key : Type::any(), valueExpected ? valueExpected : value ? value : Type::any()));
        }
        case ExprKind::unary: {
            auto operand = expression(*e.child(0), nullptr);
            if (e.op == "-") {
                if (operand && !operand->isAny() && !operand->numeric() && !(operand->kind == Type::Kind::host && (operand->name == "Vec3" || operand->name == "Vec2")))
                    error(tr("Il segno meno richiede un numero"), e.range, {}, "Number", operand->describe());
                return record(e, operand);
            }
            if (operand && !operand->isAny() && operand->kind != Type::Kind::boolean) error(tr("«!» richiede un valore vero o falso"), e.range, {}, "Bool", operand->describe());
            return record(e, Type::boolean());
        }
        case ExprKind::binary: return record(e, binary(e));
        case ExprKind::call: return record(e, call(e));
        case ExprKind::member: return record(e, memberAccess(e, false));
        case ExprKind::index: {
            auto owner = expression(*e.child(0), nullptr);
            auto key = expression(*e.child(1), nullptr);
            TypePtr base = unwrapOptional(owner);
            if (owner && owner->kind == Type::Kind::optional && !e.optionalChain) error(tr("Il valore può essere none"), e.child(0)->range, tr("Controllalo prima con «if x != none»."));
            if (!base || base->isAny()) return record(e, Type::any());
            if (base->kind == Type::Kind::list) {
                if (key && !key->isAny() && key->kind != Type::Kind::integer) error(tr("L’indice di una lista deve essere un intero"), e.child(1)->range, {}, "Integer", key->describe());
                return record(e, base->arguments[0]);
            }
            if (base->kind == Type::Kind::map) {
                if (key && !assignable(key, base->arguments[0], registry)) error(tr("Chiave non compatibile con la mappa"), e.child(1)->range, {}, base->arguments[0]->describe(), key->describe());
                return record(e, Type::optional(base->arguments[1]));
            }
            if (base->kind == Type::Kind::text) return record(e, Type::text());
            error(tr("«{0}» non si può indicizzare con []", base->describe()), e.range);
            return record(e, Type::error());
        }
        case ExprKind::lambda: return record(e, lambdaBody(e, expected ? unwrapOptional(expected) : nullptr));
        case ExprKind::await: {
            analysis.usesAwait = true;
            if (!canAwait()) error(tr("«await» si può usare solo in una funzione async"), e.range, tr("Dichiara la funzione con «async fn»."), {}, {}, "async");
            auto operand = expression(*e.child(0), nullptr);
            if (operand && operand->kind == Type::Kind::task) return record(e, operand->result);
            if (operand && operand->kind == Type::Kind::host && operand->name == "Event") return record(e, Type::any());
            if (operand && !operand->isAny()) warning(tr("«await» su un valore che non è un’attività: non c’è niente da attendere"), e.range);
            return record(e, operand);
        }
        case ExprKind::is: {
            expression(*e.child(0), nullptr);
            resolveType(e.type);
            return record(e, Type::boolean());
        }
        case ExprKind::cast: {
            expression(*e.child(0), nullptr);
            return record(e, resolveType(e.type));
        }
        case ExprKind::invalid: return record(e, Type::error());
        }
        return record(e, Type::any());
    }

    TypePtr identifier(const Expr& e) {
        if (auto id = lookup(e.text)) {
            const Symbol& symbol = analysis.symbols[static_cast<std::size_t>(*id)];
            analysis.references.push_back({e.range, symbol.id});
            analysis.resolved[&e] = symbol.id;
            if (auto narrowed = narrowedType(e.text)) return *narrowed;
            return symbol.type;
        }
        if (const HostGlobal* g = registry.findGlobal(e.text)) {
            checkContext(g->only, g->name, e.range);
            return g->type;
        }
        if (registry.findFunction(e.text)) {
            const HostMethod* f = registry.findFunction(e.text);
            return Type::function(f->parameters, f->result, f->async, f->variadic ? 0 : f->minimum, f->parameterNames);
        }
        if (const HostClass* c = registry.findClass(e.text); c && (c->constructor || c->creatable || !c->statics.empty() || !c->constants.empty())) return classReference(c->name);
        if (registry.findEnum(e.text)) return enumReference(e.text);
        std::string hint = suggest(e.text);
        error(tr("Nome sconosciuto «{0}»", e.text), e.range, hint.empty() ? tr("Dichiaralo con «let» o «var» prima di usarlo.") : tr("Forse intendevi «{0}»?", hint), {}, {}, "nome");
        return Type::error();
    }

    TypePtr shorthand(const Expr& e, const TypePtr& expected) {
        TypePtr target = expected ? unwrapOptional(expected) : nullptr;
        if (target && target->kind == Type::Kind::enumeration) {
            const std::vector<std::string>* cases = nullptr;
            if (auto it = analysis.enums.find(target->name); it != analysis.enums.end()) cases = &it->second;
            else cases = registry.findEnum(target->name);
            if (cases && std::find(cases->begin(), cases->end(), e.text) == cases->end()) {
                error(tr("«.{0}» non è un caso di {1}", e.text, target->name), e.range, tr("Casi disponibili: .{0}", joinNames(*cases)));
                return Type::error();
            }
            return target;
        }
        if (target && !target->isAny()) {
            error(tr("«.{0}» qui non ha un tipo enum da cui partire", e.text), e.range, tr("Scrivi il nome completo, per esempio State.{0}.", e.text));
            return Type::error();
        }
        return Type::any();
    }

    TypePtr arithmetic(const std::string& op, const TypePtr& left, const TypePtr& right, Range range) {
        if (!left || !right || left->isAny() || right->isAny()) {
            if (op == "+" && ((left && left->kind == Type::Kind::text) || (right && right->kind == Type::Kind::text))) return Type::text();
            return Type::any();
        }
        if (left->numeric() && right->numeric()) {
            if (op == "/") return Type::number();
            return left->kind == Type::Kind::integer && right->kind == Type::Kind::integer ? Type::integer() : Type::number();
        }
        if (op == "+" && left->kind == Type::Kind::text && right->kind == Type::Kind::text) return Type::text();
        auto vector = [](const TypePtr& t) { return t->kind == Type::Kind::host && (t->name == "Vec3" || t->name == "Vec2" || t->name == "Color"); };
        if (vector(left) && vector(right) && left->name == right->name && (op == "+" || op == "-")) return left;
        if (vector(left) && right->numeric() && (op == "*" || op == "/")) return left;
        if (left->numeric() && vector(right) && op == "*") return right;
        if (op == "+" && (left->kind == Type::Kind::text || right->kind == Type::Kind::text)) {
            error(tr("Non si può unire testo e {0} con +", (left->kind == Type::Kind::text ? right : left)->describe()), range, tr("Usa l’interpolazione: \"testo \\(valore)\"."), {}, {}, "tipo");
            return Type::text();
        }
        error(tr("Operatore «{0}» non applicabile a {1} e {2}", op, left->describe(), right->describe()), range);
        return Type::error();
    }

    TypePtr binary(const Expr& e) {
        const std::string& op = e.op;
        if (op == "??") {
            auto left = expression(*e.child(0), nullptr);
            auto inner = unwrapOptional(left);
            auto right = expression(*e.child(1), inner);
            if (left && left->kind != Type::Kind::optional && !left->isAny()) warning(tr("Il valore a sinistra di «??» non è mai none"), e.child(0)->range);
            if (inner && right && !inner->isAny() && !assignable(right, inner, registry) && !assignable(inner, right, registry)) error(tr("I due lati di «??» hanno tipi diversi"), e.range, {}, inner->describe(), right->describe());
            return right && right->kind == Type::Kind::optional ? right : inner && !inner->isAny() ? inner : right;
        }
        if (op == "&&" || op == "||") {
            auto left = expression(*e.child(0), Type::boolean());
            std::map<std::string, TypePtr> positive, negative;
            if (op == "&&") narrowing(*e.child(0), positive, negative);
            pushScope(e.range);
            scopes.back().narrowed = positive;
            auto right = expression(*e.child(1), Type::boolean());
            popScope();
            for (auto [side, t] : {std::pair{e.child(0), left}, std::pair{e.child(1), right}})
                if (t && !t->isAny() && t->kind != Type::Kind::boolean) error(tr("«{0}» richiede valori veri o falsi", op), side->range, {}, "Bool", t->describe());
            return Type::boolean();
        }
        auto left = expression(*e.child(0), nullptr);
        auto right = expression(*e.child(1), e.child(1)->kind == ExprKind::shorthand ? left : nullptr);
        if (op == "==" || op == "!=") {
            if (left && right && !left->isAny() && !right->isAny() && !assignable(left, right, registry) && !assignable(right, left, registry) && left->kind != Type::Kind::none && right->kind != Type::Kind::none)
                warning(tr("Confronto tra tipi diversi: il risultato è sempre ") + std::string(op == "==" ? "falso" : "vero"), e.range);
            return Type::boolean();
        }
        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            bool ok = (left->isAny() || left->numeric() || left->kind == Type::Kind::text) && (right->isAny() || right->numeric() || right->kind == Type::Kind::text);
            if (!ok) error(tr("«{0}» confronta solo numeri o testi", op), e.range, {}, "Number", (left->numeric() ? right : left)->describe());
            return Type::boolean();
        }
        if (op == "..." || op == "..<") {
            for (auto [side, t] : {std::pair{e.child(0), left}, std::pair{e.child(1), right}})
                if (t && !t->isAny() && t->kind != Type::Kind::integer) error(tr("Gli estremi di un intervallo devono essere interi"), side->range, {}, "Integer", t->describe());
            return Type::list(Type::integer());
        }
        return arithmetic(op, left, right, e.range);
    }

    TypePtr memberAccess(const Expr& e, bool assigning) {
        auto ownerType = expression(*e.child(0), nullptr);
        TypePtr owner = unwrapOptional(ownerType);
        if (ownerType && ownerType->kind == Type::Kind::optional && !e.optionalChain)
            error(tr("«{0}» può essere none", describeOwner(*e.child(0))), e.child(0)->range, tr("Usa «?.{0}» oppure controlla prima con «if {1} != none».", e.text, describeOwner(*e.child(0))), {}, {}, "opzionale");
        auto finish = [&](TypePtr t) { return e.optionalChain || (ownerType && ownerType->kind == Type::Kind::optional) ? Type::optional(t) : t; };
        if (!owner || owner->isAny()) return Type::any();
        if (owner->kind == Type::Kind::module) {
            auto it = modules.find(owner->name);
            if (it != modules.end()) {
                if (auto exported = it->second.exports.find(e.text); exported != it->second.exports.end()) {
                    if (assigning) error(tr("I valori di un modulo si leggono ma non si assegnano da fuori"), e.range);
                    return exported->second;
                }
                error(tr("Il modulo «{0}» non esporta «{1}»", owner->name, e.text), e.range, {}, {}, {}, "membro");
                return Type::error();
            }
            return Type::any();
        }
        if (isEnumReference(owner)) {
            std::string name = owner->name.substr(1);
            const std::vector<std::string>* cases = nullptr;
            if (auto it = analysis.enums.find(name); it != analysis.enums.end()) cases = &it->second;
            else cases = registry.findEnum(name);
            if (cases && std::find(cases->begin(), cases->end(), e.text) != cases->end()) return Type::enumeration(name);
            if (e.text == "all") return Type::list(Type::enumeration(name));
            error(tr("«{0}» non è un caso di {1}", e.text, name), e.range, cases ? tr("Casi disponibili: {0}", joinNames(*cases)) : "", {}, {}, "membro");
            return Type::error();
        }
        if (isClassReference(owner)) {
            const HostClass* c = registry.findClass(owner->name.substr(1));
            if (c) {
                if (auto s = c->statics.find(e.text); s != c->statics.end()) {
                    checkContext(s->second.only, c->name + "." + e.text, e.range);
                    return methodType(s->second);
                }
                if (auto k = c->constants.find(e.text); k != c->constants.end()) return k->second.type;
            }
            error(tr("«{0}» non è un membro di {1}", e.text, owner->name.substr(1)), e.range, suggestionFor(owner, e.text), {}, {}, "membro");
            return Type::error();
        }
        if (isUserClassReference(owner)) {
            error(tr("Per usare «{0}» crea prima un oggetto: {1}()", e.text, owner->name.substr(1)), e.range);
            return Type::error();
        }
        if (owner->kind == Type::Kind::user) {
            auto it = analysis.classes.find(owner->name);
            if (it != analysis.classes.end()) {
                for (const auto& [name, type] : it->second.fields)
                    if (name == e.text) {
                        if (assigning && !it->second.mutableFields.at(name) && !isInit()) error(tr("Il campo «{0}» è una costante", name), e.range, {}, {}, {}, "costante");
                        return finish(type);
                    }
                if (auto m = it->second.methods.find(e.text); m != it->second.methods.end()) return finish(m->second);
            }
            error(tr("«{0}» non è un membro di {1}", e.text, owner->name), e.range, suggestionFor(owner, e.text), {}, {}, "membro");
            return Type::error();
        }
        if (owner->kind == Type::Kind::host) {
            if (const HostMember* p = registry.property(owner->name, e.text)) {
                checkContext(p->only, owner->name + "." + e.text, e.range);
                if (assigning && p->readOnly) error(tr("«{0}» di {1} è di sola lettura", e.text, owner->name), e.range, {}, {}, {}, "sola lettura");
                if (p->deprecated) deprecated(e.text, p->replacement, e.range);
                return finish(p->type);
            }
            if (const HostMethod* m = registry.method(owner->name, e.text)) {
                checkContext(m->only, owner->name + "." + e.text, e.range);
                if (assigning) error(tr("Un metodo non si può assegnare"), e.range);
                if (m->deprecated) deprecated(e.text, m->replacement, e.range);
                return finish(methodType(*m));
            }
            if (const HostEvent* event = registry.event(owner->name, e.text)) {
                if (event->deprecated) deprecated(e.text, event->replacement, e.range);
                return finish(Type::host("Event"));
            }
            switch (registry.subclassMember(owner->name, e.text)) {
            case MemberKind::event: return finish(Type::host("Event"));
            case MemberKind::property:
            case MemberKind::method: return finish(Type::any());
            case MemberKind::none: break;
            }
            if (const HostClass* c = registry.findClass(owner->name)) {
                if (auto st = c->statics.find(e.text); st != c->statics.end()) {
                    checkContext(st->second.only, owner->name + "." + e.text, e.range);
                    if (assigning) error(tr("Un metodo non si può assegnare"), e.range);
                    return methodType(st->second);
                }
                if (auto k = c->constants.find(e.text); k != c->constants.end()) {
                    if (assigning) error(tr("«{0}» è una costante", e.text), e.range, {}, {}, {}, "costante");
                    return k->second.type;
                }
            }
            error(tr("«{0}» non è un membro di {1}", e.text, owner->name), e.range, suggestionFor(owner, e.text), {}, {}, "membro");
            return Type::error();
        }
        if (auto builtin = builtinMember(owner, e.text)) {
            if (assigning) error(tr("«{0}» non si può assegnare", e.text), e.range);
            return finish(builtin);
        }
        error(tr("«{0}» non è un membro di {1}", e.text, owner->describe()), e.range, suggestionFor(owner, e.text), {}, {}, "membro");
        return Type::error();
    }

    std::string describeOwner(const Expr& e) {
        if (e.kind == ExprKind::identifier) return e.text;
        if (e.kind == ExprKind::member) return describeOwner(*e.child(0)) + "." + e.text;
        return "valore";
    }

    std::string suggestionFor(const TypePtr& owner, const std::string& name) {
        std::vector<std::string> candidates;
        if (owner->kind == Type::Kind::host) {
            std::string className = isClassReference(owner) ? owner->name.substr(1) : owner->name;
            if (isClassReference(owner)) {
                if (const HostClass* c = registry.findClass(className)) {
                    for (const auto& [n, m] : c->statics) candidates.push_back(n);
                    for (const auto& [n, m] : c->constants) candidates.push_back(n);
                }
            } else {
                for (auto* p : registry.properties(className)) candidates.push_back(p->name);
                for (auto* m : registry.methods(className)) candidates.push_back(m->name);
                for (auto* ev : registry.events(className)) candidates.push_back(ev->name);
            }
        } else if (owner->kind == Type::Kind::user) {
            auto it = analysis.classes.find(owner->name);
            if (it != analysis.classes.end()) {
                for (const auto& f : it->second.fields) candidates.push_back(f.first);
                for (const auto& [n, m] : it->second.methods) candidates.push_back(n);
            }
        } else candidates = builtinMembers(owner);
        std::string best;
        std::size_t score = std::max<std::size_t>(2, name.size() / 3) + 1;
        for (const auto& c : candidates) {
            std::size_t d = editDistance(name, c);
            if (d < score) { score = d; best = c; }
        }
        return best.empty() ? std::string() : tr("Forse intendevi «{0}»?", best);
    }

    TypePtr methodType(const HostMethod& m) {
        return Type::function(m.parameters, m.async ? Type::task(m.result) : m.result, false, m.variadic ? 0 : m.minimum, m.parameterNames);
    }

    TypePtr call(const Expr& e) {
        const Expr* callee = e.child(0);
        TypePtr calleeType = expression(*callee, nullptr);
        TypePtr base = unwrapOptional(calleeType);
        if (calleeType && calleeType->kind == Type::Kind::optional && !(callee->kind == ExprKind::member && callee->optionalChain))
            error(tr("Questa funzione può essere none"), callee->range, tr("Controllala prima con «if»."));
        auto finish = [&](TypePtr t) { return calleeType && calleeType->kind == Type::Kind::optional ? Type::optional(t) : t; };
        if (!base || base->isAny()) {
            for (const auto& a : e.arguments) expression(*a.value, nullptr);
            return Type::any();
        }
        if (isClassReference(base)) {
            const HostClass* c = registry.findClass(base->name.substr(1));
            if (!c || !c->constructor) {
                for (const auto& a : e.arguments) expression(*a.value, nullptr);
                if (c && c->creatable) return Type::host(c->name);
                error(tr("«{0}» non si può creare con ()", base->name.substr(1)), callee->range, {}, {}, {}, "costruttore");
                return Type::error();
            }
            checkArguments(e, c->constructor->parameters, c->constructor->parameterNames, c->constructor->minimum, c->constructor->variadic, c->name);
            return Type::host(c->name);
        }
        if (isUserClassReference(base)) {
            std::string name = base->name.substr(1);
            const UserClass& c = analysis.classes[name];
            if (c.initializer) {
                const TypePtr& init = *c.initializer;
                checkArguments(e, init->arguments, init->parameterNames, init->minimumArguments, false, name);
            } else {
                for (const auto& a : e.arguments) {
                    if (a.label.empty()) {
                        error(tr("Per creare un oggetto indica i campi per nome, per esempio {0}(", name) + (c.fields.empty() ? "campo" : c.fields[0].first) + tr(": valore)"), a.range);
                        expression(*a.value, nullptr);
                        continue;
                    }
                    auto field = std::find_if(c.fields.begin(), c.fields.end(), [&](const auto& f) { return f.first == a.label; });
                    if (field == c.fields.end()) {
                        error(tr("«{0}» non è un campo di {1}", a.label, name), a.range);
                        expression(*a.value, nullptr);
                        continue;
                    }
                    auto t = expression(*a.value, field->second);
                    if (!assignable(t, field->second, registry)) error(tr("Valore non compatibile per il campo «{0}»", a.label), a.value->range, typeHelp(t, field->second), field->second->describe(), t->describe());
                }
            }
            return Type::user(name);
        }
        if (base->kind == Type::Kind::function) {
            checkArguments(e, base->arguments, base->parameterNames, base->minimumArguments, base->minimumArguments == 0 && callee->kind == ExprKind::identifier && registry.findFunction(callee->text) && registry.findFunction(callee->text)->variadic, "");
            if (base->async && e.kind == ExprKind::call) return finish(Type::task(base->result));
            return finish(base->result ? base->result : Type::any());
        }
        for (const auto& a : e.arguments) expression(*a.value, nullptr);
        error(tr("«{0}» non si può chiamare", describeOwner(*callee)), callee->range, base->kind == Type::Kind::host && base->name == "Event" ? tr("Per reagire a un evento scrivi «on oggetto.evento(…) { }».") : "", {}, {}, "chiamata");
        return Type::error();
    }

    void checkArguments(const Expr& e, const std::vector<TypePtr>& parameters, const std::vector<std::string>& names, int minimum, bool variadic, const std::string& owner) {
        std::size_t positional = 0;
        std::vector<bool> filled(parameters.size(), false);
        for (const auto& a : e.arguments) {
            std::size_t index = positional;
            if (!a.label.empty()) {
                auto it = std::find(names.begin(), names.end(), a.label);
                if (it == names.end()) {
                    error(tr("Parametro «{0}» sconosciuto", a.label), a.range, names.empty() ? "" : "Parametri: " + joinNames(names), {}, {}, "argomenti");
                    expression(*a.value, nullptr);
                    continue;
                }
                index = static_cast<std::size_t>(it - names.begin());
            } else ++positional;
            if (index >= parameters.size()) {
                if (!variadic) error(tr("Troppi argomenti") + (owner.empty() ? std::string() : " per " + owner) + tr(": ne servono al massimo {0}", std::to_string(parameters.size())), a.range, {}, std::to_string(parameters.size()), std::to_string(e.arguments.size()), "argomenti");
                expression(*a.value, nullptr);
                continue;
            }
            filled[index] = true;
            auto t = expression(*a.value, parameters[index]);
            if (!assignable(t, parameters[index], registry))
                error(tr("Argomento non compatibile") + (index < names.size() ? tr(" per «{0}»", names[index]) : std::string()), a.value->range, typeHelp(t, parameters[index]), parameters[index]->describe(), t->describe(), "argomenti");
        }
        if (!variadic) {
            int count = static_cast<int>(std::count(filled.begin(), filled.end(), true));
            if (count < minimum) {
                std::string missing;
                for (int i = 0; i < minimum && i < static_cast<int>(names.size()); ++i)
                    if (!filled[static_cast<std::size_t>(i)]) missing += (missing.empty() ? "" : ", ") + names[static_cast<std::size_t>(i)];
                error(tr("Argomenti mancanti{0}", (missing.empty() ? std::string() : ": " + missing)), e.range, {}, std::to_string(minimum), std::to_string(count), "argomenti");
            }
        }
    }
};

}

Analysis analyze(const Program& program, const TypeRegistry& registry, const AnalyzeOptions& options) { return Analyzer(registry, options).run(program); }

}
