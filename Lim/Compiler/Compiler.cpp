#include "Compiler.hpp"

#include "Lim/Parser/Parser.hpp"
#include "Localization/Localization.hpp"

#include <limits>
#include <map>
#include <set>

namespace lim {

using ld::tr;

namespace {

constexpr std::uint32_t noDefault = std::numeric_limits<std::uint32_t>::max();

struct ClassContext {
    std::set<std::string> members;
};

class Resolver {
public:
    std::set<const void*> captured;

    void program(const Program& p) {
        functions.push_back({});
        functions.back().main = true;
        pushScope();
        functions.back().scopes.back().global = true;
        hoist(p.statements);
        for (const auto& s : p.statements) statement(*s);
        popScope();
    }

private:
    struct Scope {
        std::map<std::string, const void*> names;
        bool global = false;
    };
    struct Function {
        std::vector<Scope> scopes;
        bool main = false;
        const ClassContext* classContext = nullptr;
    };
    std::vector<Function> functions;
    std::vector<std::unique_ptr<ClassContext>> classContexts;

    void pushScope() { functions.back().scopes.push_back({}); }
    void popScope() { functions.back().scopes.pop_back(); }
    void declare(const std::string& name, const void* key) { functions.back().scopes.back().names[name] = key; }

    void use(const std::string& name) {
        for (std::size_t f = functions.size(); f-- > 0;) {
            auto& function = functions[f];
            for (auto scope = function.scopes.rbegin(); scope != function.scopes.rend(); ++scope) {
                auto it = scope->names.find(name);
                if (it == scope->names.end()) continue;
                if (scope->global) return;
                if (f + 1 != functions.size()) captured.insert(it->second);
                return;
            }
            if (function.classContext && function.classContext->members.count(name)) {
                use("self");
                return;
            }
        }
    }

    void hoist(const std::vector<StmtPtr>& statements) {
        for (const auto& s : statements)
            if (s->kind == StmtKind::function || s->kind == StmtKind::classDecl || s->kind == StmtKind::enumDecl) declare(s->name, s.get());
    }

    void block(const Block& b) {
        pushScope();
        hoist(b.statements);
        for (const auto& s : b.statements) statement(*s);
        popScope();
    }

    void function(const std::vector<Parameter>& parameters, const Block* body, const ClassContext* context, const void* selfKey) {
        Function f;
        f.classContext = context ? context : (functions.empty() ? nullptr : functions.back().classContext);
        functions.push_back(std::move(f));
        pushScope();
        if (selfKey) declare("self", selfKey);
        for (const auto& p : parameters) {
            if (p.fallback) expression(*p.fallback);
            declare(p.name, &p);
        }
        if (body) {
            hoist(body->statements);
            for (const auto& s : body->statements) statement(*s);
        }
        popScope();
        functions.pop_back();
    }

    void statement(const Stmt& s) {
        switch (s.kind) {
        case StmtKind::declaration:
            if (s.value) expression(*s.value);
            if (!functions.back().scopes.back().global) declare(s.name, &s);
            break;
        case StmtKind::function: function(s.function->parameters, s.function->body.get(), nullptr, nullptr); break;
        case StmtKind::classDecl: {
            auto context = std::make_unique<ClassContext>();
            for (const auto& f : s.fields) context->members.insert(f.name);
            for (const auto& m : s.methods) context->members.insert(m->name);
            const ClassContext* raw = context.get();
            classContexts.push_back(std::move(context));
            const void* selfKey = nullptr;
            for (const auto& f : s.fields)
                if (f.initializer) { selfKey = &f; break; }
            if (selfKey) {
                functions.push_back({});
                functions.back().classContext = raw;
                pushScope();
                declare("self", selfKey);
                for (const auto& f : s.fields)
                    if (f.initializer) expression(*f.initializer);
                popScope();
                functions.pop_back();
            }
            for (const auto& m : s.methods) function(m->parameters, m->body.get(), raw, m.get());
            break;
        }
        case StmtKind::enumDecl:
        case StmtKind::import:
        case StmtKind::breakStmt:
        case StmtKind::continueStmt: break;
        case StmtKind::ifStmt:
            expression(*s.value);
            block(*s.body);
            if (s.otherwise) block(*s.otherwise);
            break;
        case StmtKind::whileStmt:
            expression(*s.value);
            block(*s.body);
            break;
        case StmtKind::forStmt:
            expression(*s.value);
            pushScope();
            for (const auto& n : s.names) declare(n, &n);
            hoist(s.body->statements);
            for (const auto& st : s.body->statements) statement(*st);
            popScope();
            break;
        case StmtKind::matchStmt:
            expression(*s.value);
            for (const auto& c : s.cases) {
                for (const auto& p : c.patterns) expression(*p);
                block(*c.body);
            }
            break;
        case StmtKind::returnStmt:
            if (s.value) expression(*s.value);
            break;
        case StmtKind::expression: expression(*s.value); break;
        case StmtKind::assign:
            expression(*s.target);
            expression(*s.value);
            break;
        case StmtKind::on:
            expression(*s.target);
            function(s.parameters, s.body.get(), nullptr, nullptr);
            break;
        case StmtKind::block: block(*s.body); break;
        }
    }

    void expression(const Expr& e) {
        switch (e.kind) {
        case ExprKind::identifier: use(e.text); break;
        case ExprKind::self: use("self"); break;
        case ExprKind::lambda: function(e.parameters, e.body.get(), nullptr, nullptr); break;
        default: break;
        }
        for (const auto& c : e.children)
            if (c) expression(*c);
        for (const auto& [k, v] : e.entries) {
            if (k) expression(*k);
            if (v) expression(*v);
        }
        for (const auto& a : e.arguments)
            if (a.value) expression(*a.value);
    }
};

class Compiler {
public:
    Compiler(const Analysis& analysis, std::set<const void*> captured) : analysis(analysis), captured(std::move(captured)) {}

    Module run(const Program& program, const std::string& name, ExecutionContext context) {
        module.name = name;
        module.file = program.file;
        module.context = std::string(contextName(context));
        auto main = beginFunction("main", nullptr, true);
        main->proto.async = true;
        pushScope(true);
        hoistBlock(program.statements, true);
        for (const auto& s : program.statements) statement(*s, true);
        popScope();
        emit(Op::returnNone, {});
        module.main = finishFunction();
        for (const auto& s : program.statements)
            if (s->kind == StmtKind::declaration || s->kind == StmtKind::function || s->kind == StmtKind::classDecl || s->kind == StmtKind::enumDecl) module.exports.push_back(s->name);
        return std::move(module);
    }

private:
    const Analysis& analysis;
    std::set<const void*> captured;
    Module module;

    struct Local {
        std::uint32_t slot = 0;
        bool cell = false;
        const void* key = nullptr;
    };
    struct Scope {
        std::map<std::string, Local> names;
        bool global = false;
    };
    struct Loop {
        std::vector<std::size_t> breaks;
        std::vector<std::size_t> continues;
        std::size_t start = 0;
        bool startKnown = false;
    };
    struct FunctionState {
        Prototype proto;
        FunctionState* parent = nullptr;
        std::vector<Scope> scopes;
        std::uint32_t nextSlot = 0;
        std::vector<const void*> captureKeys;
        std::vector<Loop> loops;
        bool main = false;
        std::set<std::string> classMembers;
        bool hasClass = false;
        std::size_t index = 0;
    };
    std::vector<std::unique_ptr<FunctionState>> stack;
    Range current;

    FunctionState* fs() { return stack.back().get(); }

    FunctionState* beginFunction(const std::string& name, FunctionState* parent, bool main = false) {
        auto state = std::make_unique<FunctionState>();
        state->parent = parent;
        state->main = main;
        state->proto.name = name;
        if (parent && parent->hasClass) {
            state->classMembers = parent->classMembers;
            state->hasClass = true;
        }
        state->index = module.prototypes.size();
        module.prototypes.emplace_back();
        stack.push_back(std::move(state));
        return stack.back().get();
    }

    std::uint32_t finishFunction() {
        auto state = std::move(stack.back());
        stack.pop_back();
        state->proto.locals = std::max(state->proto.locals, state->nextSlot);
        state->proto.maxStack = measureStack(state->proto) + 2;
        std::size_t index = state->index;
        module.prototypes[index] = std::move(state->proto);
        return static_cast<std::uint32_t>(index);
    }

    void pushScope(bool global = false) {
        Scope s;
        s.global = global;
        fs()->scopes.push_back(std::move(s));
    }

    void popScope() { fs()->scopes.pop_back(); }

    std::size_t emit(Op op, Range range, std::uint32_t a = 0, std::uint32_t b = 0) {
        Prototype& p = fs()->proto;
        p.code.push_back({op, a, b});
        Range where = range.start.line ? range : current;
        p.lines.push_back(where.start.line);
        p.columns.push_back(where.start.column);
        return p.code.size() - 1;
    }

    void patch(std::size_t at) { fs()->proto.code[at].a = static_cast<std::uint32_t>(fs()->proto.code.size()); }
    std::size_t here() const { return stack.back()->proto.code.size(); }

    std::uint32_t constantText(const std::string& text) {
        auto& constants = fs()->proto.constants;
        for (std::size_t i = 0; i < constants.size(); ++i)
            if (constants[i].kind == Constant::Kind::text && constants[i].text == text) return static_cast<std::uint32_t>(i);
        Constant c;
        c.kind = Constant::Kind::text;
        c.text = text;
        constants.push_back(c);
        return static_cast<std::uint32_t>(constants.size() - 1);
    }

    std::uint32_t constant(Constant c) {
        auto& constants = fs()->proto.constants;
        for (std::size_t i = 0; i < constants.size(); ++i)
            if (constants[i] == c) return static_cast<std::uint32_t>(i);
        constants.push_back(std::move(c));
        return static_cast<std::uint32_t>(constants.size() - 1);
    }

    std::uint32_t allocate() {
        std::uint32_t slot = fs()->nextSlot++;
        fs()->proto.locals = std::max(fs()->proto.locals, fs()->nextSlot);
        return slot;
    }

    Local declareLocal(const std::string& name, const void* key) {
        Local local;
        local.slot = allocate();
        fs()->proto.localNames.push_back({local.slot, static_cast<std::uint32_t>(fs()->proto.code.size()), name});
        local.cell = captured.count(key) > 0;
        local.key = key;
        fs()->scopes.back().names[name] = local;
        return local;
    }

    void storeNew(const Local& local, Range range) { emit(local.cell ? Op::makeCell : Op::setLocal, range, local.slot); }

    bool atGlobalScope() const { return stack.back()->main && stack.back()->scopes.size() == 1; }

    void defineName(const std::string& name, const void* key, bool mutable_, Range range) {
        if (atGlobalScope()) emit(Op::defineGlobal, range, constantText(name), mutable_ ? 1 : 0);
        else storeNew(declareLocal(name, key), range);
    }

    struct Resolution {
        enum class Kind { local, upvalue, global, member } kind = Kind::global;
        std::uint32_t index = 0;
        bool cell = false;
    };

    std::optional<Local> findLocal(FunctionState* state, const std::string& name, bool* global) {
        for (auto scope = state->scopes.rbegin(); scope != state->scopes.rend(); ++scope) {
            auto it = scope->names.find(name);
            if (it != scope->names.end()) return it->second;
            if (scope->global && state->main) {
                *global = true;
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<std::uint32_t> upvalue(FunctionState* state, const std::string& name) {
        if (!state->parent) return std::nullopt;
        bool global = false;
        if (auto local = findLocal(state->parent, name, &global)) {
            if (!local->cell) return std::nullopt;
            return addCapture(state, local->key, true, local->slot);
        }
        if (global) return std::nullopt;
        if (auto outer = upvalue(state->parent, name)) {
            const void* key = state->parent->captureKeys[*outer];
            return addCapture(state, key, false, *outer);
        }
        return std::nullopt;
    }

    std::uint32_t addCapture(FunctionState* state, const void* key, bool fromLocal, std::uint32_t index) {
        for (std::size_t i = 0; i < state->captureKeys.size(); ++i)
            if (state->captureKeys[i] == key) return static_cast<std::uint32_t>(i);
        state->captureKeys.push_back(key);
        state->proto.captures.push_back({fromLocal, index});
        return static_cast<std::uint32_t>(state->captureKeys.size() - 1);
    }

    Resolution resolve(const std::string& name) {
        bool global = false;
        if (auto local = findLocal(fs(), name, &global)) return {Resolution::Kind::local, local->slot, local->cell};
        if (!global) {
            if (auto up = upvalue(fs(), name)) return {Resolution::Kind::upvalue, *up, true};
        }
        if (fs()->hasClass && fs()->classMembers.count(name)) return {Resolution::Kind::member, 0, false};
        return {Resolution::Kind::global, constantText(name), false};
    }

    void loadSelf(Range range) {
        auto r = resolve("self");
        loadResolved(r, "self", range);
    }

    void loadResolved(const Resolution& r, const std::string& name, Range range) {
        switch (r.kind) {
        case Resolution::Kind::local: emit(r.cell ? Op::getCell : Op::getLocal, range, r.index); break;
        case Resolution::Kind::upvalue: emit(Op::getUpvalue, range, r.index); break;
        case Resolution::Kind::global: emit(Op::getGlobal, range, r.index); break;
        case Resolution::Kind::member:
            loadSelf(range);
            emit(Op::getMember, range, constantText(name));
            break;
        }
    }

    void load(const std::string& name, Range range) { loadResolved(resolve(name), name, range); }

    void hoistBlock(const std::vector<StmtPtr>& statements, bool topLevel) {
        for (const auto& s : statements) {
            if (s->kind != StmtKind::function && s->kind != StmtKind::classDecl && s->kind != StmtKind::enumDecl) continue;
            if (topLevel) continue;
            declareLocal(s->name, s.get());
            if (captured.count(s.get())) {
                emit(Op::none, s->range);
                emit(Op::makeCell, s->range, fs()->scopes.back().names[s->name].slot);
            }
        }
        for (const auto& s : statements) {
            if (s->kind == StmtKind::function) {
                current = s->range;
                compileFunction(*s->function, s->name, false, nullptr);
                storeHoisted(*s, topLevel);
            }
        }
    }

    void storeHoisted(const Stmt& s, bool topLevel) {
        if (topLevel) {
            emit(Op::defineGlobal, s.range, constantText(s.name), 0);
            return;
        }
        const Local& local = fs()->scopes.back().names[s.name];
        emit(local.cell ? Op::setCell : Op::setLocal, s.range, local.slot);
    }

    std::uint32_t compileFunction(const FunctionDecl& f, const std::string& name, bool method, const void* selfKey) {
        std::uint32_t index = compileBody(name, f.parameters, f.body.get(), f.async, method, selfKey, f.isInit);
        emit(Op::closure, f.nameRange, index);
        return index;
    }

    std::uint32_t compileBody(const std::string& name, const std::vector<Parameter>& parameters, const Block* body, bool async, bool method, const void* selfKey, bool isInit) {
        FunctionState* parent = fs();
        std::vector<std::uint32_t> defaults;
        bool anyDefault = false;
        for (const auto& p : parameters) {
            if (!p.fallback) { defaults.push_back(noDefault); continue; }
            anyDefault = true;
            beginFunction(name + ".default." + p.name, parent);
            pushScope();
            expression(*p.fallback);
            emit(Op::returnValue, p.range);
            popScope();
            defaults.push_back(finishFunction());
        }
        auto state = beginFunction(name, parent);
        state->proto.async = async;
        state->proto.method = method;
        pushScope();
        std::uint32_t minimum = 0;
        if (method) {
            Local self;
            self.slot = allocate();
            self.cell = captured.count(selfKey) > 0;
            self.key = selfKey;
            fs()->scopes.back().names["self"] = self;
            state->proto.parameterNames.push_back("self");
            state->proto.cellParameters.push_back(self.cell);
            minimum = 1;
        }
        for (const auto& p : parameters) {
            Local local;
            local.slot = allocate();
            local.cell = captured.count(&p) > 0;
            local.key = &p;
            fs()->scopes.back().names[p.name] = local;
            state->proto.parameterNames.push_back(p.name);
            state->proto.cellParameters.push_back(local.cell);
            if (!p.fallback) minimum = static_cast<std::uint32_t>(state->proto.parameterNames.size());
        }
        state->proto.parameters = static_cast<std::uint32_t>(state->proto.parameterNames.size());
        state->proto.minimum = minimum;
        if (anyDefault || method) {
            std::vector<std::uint32_t> all;
            if (method) all.push_back(noDefault);
            all.insert(all.end(), defaults.begin(), defaults.end());
            state->proto.defaults = all;
        }
        if (body) {
            pushScope();
            hoistBlock(body->statements, false);
            for (const auto& s : body->statements) statement(*s, false);
            popScope();
        }
        if (isInit) {
            loadSelf({});
            emit(Op::returnValue, {});
        } else emit(Op::returnNone, body ? body->range : Range{});
        popScope();
        return finishFunction();
    }

    void block(const Block& b) {
        pushScope();
        hoistBlock(b.statements, false);
        for (const auto& s : b.statements) statement(*s, false);
        popScope();
    }

    void statement(const Stmt& s, bool topLevel) {
        current = s.range;
        switch (s.kind) {
        case StmtKind::declaration:
            if (s.value) expression(*s.value);
            else emit(Op::none, s.range);
            defineName(s.name, &s, s.mutable_, s.nameRange);
            break;
        case StmtKind::function:
            if (!topLevel && !fs()->scopes.back().names.count(s.name)) {
                compileFunction(*s.function, s.name, false, nullptr);
                storeNew(declareLocal(s.name, &s), s.range);
            }
            break;
        case StmtKind::classDecl: classDeclaration(s, topLevel); break;
        case StmtKind::enumDecl: {
            EnumDescriptor d;
            d.name = s.name;
            for (const auto& c : s.enumCases) d.cases.push_back(c.first);
            fs()->proto.enums.push_back(d);
            emit(Op::makeEnum, s.range, static_cast<std::uint32_t>(fs()->proto.enums.size() - 1));
            if (topLevel) emit(Op::defineGlobal, s.range, constantText(s.name), 0);
            else {
                const Local& local = fs()->scopes.back().names[s.name];
                emit(local.cell ? Op::setCell : Op::setLocal, s.range, local.slot);
            }
            break;
        }
        case StmtKind::import:
            emit(Op::import, s.range, constantText(s.name));
            emit(Op::defineGlobal, s.range, constantText(s.name), 0);
            break;
        case StmtKind::ifStmt: {
            expression(*s.value);
            std::size_t skip = emit(Op::jumpIfFalse, s.value->range);
            block(*s.body);
            if (s.otherwise) {
                std::size_t end = emit(Op::jump, s.range);
                patch(skip);
                block(*s.otherwise);
                patch(end);
            } else patch(skip);
            break;
        }
        case StmtKind::whileStmt: {
            Loop loop;
            loop.start = here();
            loop.startKnown = true;
            fs()->loops.push_back(loop);
            expression(*s.value);
            std::size_t exit = emit(Op::jumpIfFalse, s.value->range);
            block(*s.body);
            emit(Op::jump, s.range, static_cast<std::uint32_t>(fs()->loops.back().start));
            patch(exit);
            finishLoop(fs()->loops.back().start);
            break;
        }
        case StmtKind::forStmt: {
            expression(*s.value);
            emit(Op::iterStart, s.value->range);
            Loop loop;
            loop.start = here();
            loop.startKnown = true;
            fs()->loops.push_back(loop);
            std::size_t next = emit(Op::iterNext, s.range, 0, static_cast<std::uint32_t>(s.names.size()));
            pushScope();
            std::vector<Local> locals;
            for (const auto& n : s.names) locals.push_back(declareLocal(n, &n));
            for (std::size_t i = locals.size(); i-- > 0;) storeNew(locals[i], s.nameRanges[i]);
            hoistBlock(s.body->statements, false);
            for (const auto& st : s.body->statements) statement(*st, false);
            popScope();
            emit(Op::jump, s.range, static_cast<std::uint32_t>(fs()->loops.back().start));
            patch(next);
            std::size_t exit = here();
            emit(Op::pop, s.range);
            for (auto b : fs()->loops.back().breaks) fs()->proto.code[b].a = static_cast<std::uint32_t>(exit);
            for (auto c : fs()->loops.back().continues) fs()->proto.code[c].a = static_cast<std::uint32_t>(fs()->loops.back().start);
            fs()->loops.pop_back();
            break;
        }
        case StmtKind::matchStmt: {
            expression(*s.value);
            std::uint32_t temporary = allocate();
            emit(Op::setLocal, s.value->range, temporary);
            TypePtr subject;
            if (auto it = analysis.types.find(s.value.get()); it != analysis.types.end()) subject = it->second;
            std::vector<std::size_t> ends;
            const MatchCase* otherwise = nullptr;
            for (const auto& c : s.cases) {
                if (c.otherwise) { otherwise = &c; continue; }
                std::vector<std::size_t> hits;
                for (const auto& p : c.patterns) {
                    emit(Op::getLocal, p->range, temporary);
                    pattern(*p, subject);
                    emit(Op::equal, p->range);
                    hits.push_back(emit(Op::jumpIfTrue, p->range));
                }
                std::size_t skip = emit(Op::jump, c.range);
                for (auto h : hits) patch(h);
                block(*c.body);
                ends.push_back(emit(Op::jump, c.range));
                patch(skip);
            }
            if (otherwise) block(*otherwise->body);
            for (auto e : ends) patch(e);
            break;
        }
        case StmtKind::returnStmt:
            if (s.value) {
                expression(*s.value);
                emit(Op::returnValue, s.range);
            } else emit(Op::returnNone, s.range);
            break;
        case StmtKind::breakStmt:
            if (!fs()->loops.empty()) fs()->loops.back().breaks.push_back(emit(Op::jump, s.range));
            break;
        case StmtKind::continueStmt:
            if (!fs()->loops.empty()) fs()->loops.back().continues.push_back(emit(Op::jump, s.range));
            break;
        case StmtKind::expression:
            expression(*s.value);
            emit(Op::pop, s.range);
            break;
        case StmtKind::assign: assignment(s); break;
        case StmtKind::on: {
            const Expr& target = *s.target;
            if (target.kind == ExprKind::member) {
                expression(*target.child(0));
                emit(Op::getMember, target.range, constantText(target.text));
            } else expression(target);
            std::uint32_t index = compileBody("on " + (target.kind == ExprKind::member ? target.text : std::string("evento")), s.parameters, s.body.get(), true, false, nullptr, false);
            emit(Op::closure, s.range, index);
            emit(Op::connect, s.range);
            emit(Op::pop, s.range);
            break;
        }
        case StmtKind::block: block(*s.body); break;
        }
    }

    void finishLoop(std::size_t start) {
        std::size_t exit = here();
        for (auto b : fs()->loops.back().breaks) fs()->proto.code[b].a = static_cast<std::uint32_t>(exit);
        for (auto c : fs()->loops.back().continues) fs()->proto.code[c].a = static_cast<std::uint32_t>(start);
        fs()->loops.pop_back();
    }

    void pattern(const Expr& p, const TypePtr& subject) {
        if (p.kind == ExprKind::shorthand) {
            TypePtr base = unwrapOptional(subject);
            if (base && base->kind == Type::Kind::enumeration) {
                Constant c;
                c.kind = Constant::Kind::enumValue;
                c.enumValue = {base->name, p.text};
                emit(Op::constant, p.range, constant(c));
            } else emit(Op::shorthand, p.range, constantText(p.text));
            return;
        }
        expression(p);
    }

    void classDeclaration(const Stmt& s, bool topLevel) {
        ClassDescriptor d;
        d.name = s.name;
        for (const auto& f : s.fields) {
            d.fields.push_back(f.name);
            d.mutableFields.push_back(f.mutable_);
        }
        FunctionState* parent = fs();
        std::set<std::string> members;
        for (const auto& f : s.fields) members.insert(f.name);
        for (const auto& m : s.methods) members.insert(m->name);
        bool anyInitializer = false;
        for (const auto& f : s.fields) anyInitializer = anyInitializer || f.initializer;
        if (anyInitializer) {
            auto state = beginFunction(s.name + ".campi", parent);
            state->classMembers = members;
            state->hasClass = true;
            state->proto.method = true;
            pushScope();
            Local self;
            self.slot = allocate();
            const void* key = nullptr;
            for (const auto& f : s.fields)
                if (f.initializer) { key = &f; break; }
            self.cell = captured.count(key) > 0;
            self.key = key;
            fs()->scopes.back().names["self"] = self;
            state->proto.parameterNames = {"self"};
            state->proto.cellParameters = {self.cell};
            state->proto.parameters = 1;
            state->proto.minimum = 1;
            for (const auto& f : s.fields) {
                if (!f.initializer) continue;
                loadSelf(f.range);
                expression(*f.initializer);
                emit(Op::setMember, f.range, constantText(f.name));
            }
            emit(Op::returnNone, s.range);
            popScope();
            d.fieldInitializer = finishFunction();
            d.hasFieldInitializer = true;
        }
        for (const auto& m : s.methods) {
            FunctionState* holder = fs();
            bool saved = holder->hasClass;
            auto savedMembers = holder->classMembers;
            holder->hasClass = true;
            holder->classMembers = members;
            std::uint32_t index = compileBody(s.name + "." + m->name, m->parameters, m->body.get(), m->async, true, m.get(), m->isInit);
            holder->hasClass = saved;
            holder->classMembers = savedMembers;
            d.methods.emplace_back(m->name, index);
        }
        fs()->proto.classes.push_back(d);
        emit(Op::makeClass, s.range, static_cast<std::uint32_t>(fs()->proto.classes.size() - 1));
        if (topLevel) emit(Op::defineGlobal, s.range, constantText(s.name), 0);
        else {
            const Local& local = fs()->scopes.back().names[s.name];
            emit(local.cell ? Op::setCell : Op::setLocal, s.range, local.slot);
        }
    }

    Op arithmetic(const std::string& op) {
        if (op == "+") return Op::add;
        if (op == "-") return Op::subtract;
        if (op == "*") return Op::multiply;
        if (op == "/") return Op::divide;
        return Op::modulo;
    }

    void assignment(const Stmt& s) {
        const Expr& target = *s.target;
        bool compound = s.op != "=";
        if (target.kind == ExprKind::identifier) {
            auto r = resolve(target.text);
            if (r.kind == Resolution::Kind::member) {
                loadSelf(target.range);
                if (compound) {
                    emit(Op::dup, target.range);
                    emit(Op::getMember, target.range, constantText(target.text));
                    expression(*s.value);
                    emit(arithmetic(s.op.substr(0, 1)), s.range);
                } else expression(*s.value);
                emit(Op::setMember, target.range, constantText(target.text));
                return;
            }
            if (compound) {
                loadResolved(r, target.text, target.range);
                expression(*s.value);
                emit(arithmetic(s.op.substr(0, 1)), s.range);
            } else expression(*s.value);
            switch (r.kind) {
            case Resolution::Kind::local: emit(r.cell ? Op::setCell : Op::setLocal, target.range, r.index); break;
            case Resolution::Kind::upvalue: emit(Op::setUpvalue, target.range, r.index); break;
            case Resolution::Kind::global: emit(Op::setGlobal, target.range, r.index); break;
            case Resolution::Kind::member: break;
            }
            return;
        }
        if (target.kind == ExprKind::member) {
            expression(*target.child(0));
            if (compound) {
                emit(Op::dup, target.range);
                emit(Op::getMember, target.range, constantText(target.text));
                expression(*s.value);
                emit(arithmetic(s.op.substr(0, 1)), s.range);
            } else expression(*s.value);
            emit(Op::setMember, target.range, constantText(target.text));
            return;
        }
        if (target.kind == ExprKind::index) {
            expression(*target.child(0));
            expression(*target.child(1));
            if (compound) {
                emit(Op::dup2, target.range);
                emit(Op::getIndex, target.range);
                expression(*s.value);
                emit(arithmetic(s.op.substr(0, 1)), s.range);
            } else expression(*s.value);
            emit(Op::setIndex, target.range);
        }
    }

    void expression(const Expr& e) {
        Range saved = current;
        current = e.range;
        switch (e.kind) {
        case ExprKind::none: emit(Op::none, e.range); break;
        case ExprKind::boolean: emit(e.flag ? Op::pushTrue : Op::pushFalse, e.range); break;
        case ExprKind::integer: {
            Constant c;
            c.kind = Constant::Kind::integer;
            c.integer = e.integer;
            emit(Op::constant, e.range, constant(c));
            break;
        }
        case ExprKind::number: {
            Constant c;
            c.kind = Constant::Kind::number;
            c.number = e.number;
            emit(Op::constant, e.range, constant(c));
            break;
        }
        case ExprKind::text: emit(Op::constant, e.range, constantText(e.text)); break;
        case ExprKind::interpolation: {
            std::uint32_t count = 0;
            for (std::size_t i = 0; i < e.parts.size(); ++i) {
                if (!e.parts[i].empty()) {
                    emit(Op::constant, e.range, constantText(e.parts[i]));
                    ++count;
                }
                if (i < e.children.size()) {
                    expression(*e.children[i]);
                    ++count;
                }
            }
            emit(Op::interpolate, e.range, count);
            break;
        }
        case ExprKind::identifier: load(e.text, e.range); break;
        case ExprKind::self: loadSelf(e.range); break;
        case ExprKind::shorthand: {
            TypePtr type;
            if (auto it = analysis.types.find(&e); it != analysis.types.end()) type = unwrapOptional(it->second);
            if (type && type->kind == Type::Kind::enumeration) {
                Constant c;
                c.kind = Constant::Kind::enumValue;
                c.enumValue = {type->name, e.text};
                emit(Op::constant, e.range, constant(c));
            } else emit(Op::shorthand, e.range, constantText(e.text));
            break;
        }
        case ExprKind::list:
            for (const auto& c : e.children) expression(*c);
            emit(Op::makeList, e.range, static_cast<std::uint32_t>(e.children.size()));
            break;
        case ExprKind::map:
            for (const auto& [k, v] : e.entries) {
                expression(*k);
                expression(*v);
            }
            emit(Op::makeMap, e.range, static_cast<std::uint32_t>(e.entries.size()));
            break;
        case ExprKind::unary:
            expression(*e.child(0));
            emit(e.op == "-" ? Op::negate : Op::logicalNot, e.range);
            break;
        case ExprKind::binary: binary(e); break;
        case ExprKind::call: {
            expression(*e.child(0));
            bool named = false;
            for (const auto& a : e.arguments) named = named || !a.label.empty();
            for (const auto& a : e.arguments) expression(*a.value);
            if (named) {
                Constant c;
                c.kind = Constant::Kind::names;
                for (const auto& a : e.arguments) c.names.push_back(a.label);
                emit(Op::callNamed, e.range, static_cast<std::uint32_t>(e.arguments.size()), constant(c));
            } else emit(Op::call, e.range, static_cast<std::uint32_t>(e.arguments.size()));
            break;
        }
        case ExprKind::member:
            expression(*e.child(0));
            emit(e.optionalChain ? Op::getMemberOptional : Op::getMember, e.range, constantText(e.text));
            break;
        case ExprKind::index:
            expression(*e.child(0));
            expression(*e.child(1));
            emit(e.optionalChain ? Op::getIndexOptional : Op::getIndex, e.range);
            break;
        case ExprKind::lambda: {
            std::uint32_t index = compileBody("fn", e.parameters, e.body.get(), e.async, false, nullptr, false);
            emit(Op::closure, e.range, index);
            break;
        }
        case ExprKind::await:
            expression(*e.child(0));
            emit(Op::await, e.range);
            break;
        case ExprKind::is:
            expression(*e.child(0));
            emit(Op::isType, e.range, constantText(e.type ? describe(*e.type) : std::string("Any")));
            break;
        case ExprKind::cast:
            expression(*e.child(0));
            emit(Op::cast, e.range, constantText(e.type ? describe(*e.type) + (e.type->kind == TypeExpr::Kind::optional ? "?" : "") : std::string("Any")));
            break;        case ExprKind::invalid:
            emit(Op::none, e.range);
            break;
        }
        current = saved;
    }

    static std::string describe(const TypeExpr& t) {
        switch (t.kind) {
        case TypeExpr::Kind::named: return t.name;
        case TypeExpr::Kind::list: return "List";
        case TypeExpr::Kind::map: return "Map";
        case TypeExpr::Kind::function: return "Function";
        case TypeExpr::Kind::optional: return describe(*t.arguments[0]);
        }
        return "Any";
    }

    void binary(const Expr& e) {
        const std::string& op = e.op;
        if (op == "&&" || op == "||") {
            expression(*e.child(0));
            std::size_t skip = emit(op == "&&" ? Op::jumpIfFalseKeep : Op::jumpIfTrueKeep, e.range);
            emit(Op::pop, e.range);
            expression(*e.child(1));
            patch(skip);
            return;
        }
        if (op == "??") {
            expression(*e.child(0));
            std::size_t skip = emit(Op::jumpIfNotNoneKeep, e.range);
            emit(Op::pop, e.range);
            expression(*e.child(1));
            patch(skip);
            return;
        }
        expression(*e.child(0));
        if (e.child(1)->kind == ExprKind::shorthand) {
            TypePtr left;
            if (auto it = analysis.types.find(e.child(0)); it != analysis.types.end()) left = it->second;
            pattern(*e.child(1), left);
        } else expression(*e.child(1));
        if (op == "..." || op == "..<") { emit(Op::makeRange, e.range, op == "..." ? 1 : 0); return; }
        static const std::map<std::string, Op> table = {{"+", Op::add}, {"-", Op::subtract}, {"*", Op::multiply}, {"/", Op::divide}, {"%", Op::modulo},
                                                         {"==", Op::equal}, {"!=", Op::notEqual}, {"<", Op::less}, {"<=", Op::lessEqual}, {">", Op::greater}, {">=", Op::greaterEqual}};
        auto it = table.find(op);
        emit(it == table.end() ? Op::nop : it->second, e.range);
    }
};

}

Module compileProgram(const Program& program, const Analysis& analysis, const std::string& name, ExecutionContext context) {
    Resolver resolver;
    resolver.program(program);
    return Compiler(analysis, std::move(resolver.captured)).run(program, name, context);
}

CompileResult compileSource(std::string_view source, const std::string& name, const TypeRegistry& registry, const AnalyzeOptions& options) {
    CompileResult result;
    auto parsed = parse(source, options.file);
    result.diagnostics = parsed.diagnostics;
    if (hasErrors(parsed.diagnostics)) return result;
    result.analysis = analyze(parsed.program, registry, options);
    result.diagnostics.insert(result.diagnostics.end(), result.analysis.diagnostics.begin(), result.analysis.diagnostics.end());
    if (hasErrors(result.diagnostics)) return result;
    Module module = compileProgram(parsed.program, result.analysis, name, options.context);
    if (auto problem = validate(module)) {
        Diagnostic d;
        d.code = "interno";
        d.message = tr("Errore interno del compilatore: {0}", *problem);
        d.file = options.file;
        result.diagnostics.push_back(d);
        return result;
    }
    result.module = std::move(module);
    return result;
}

}
