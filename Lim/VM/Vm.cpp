#include "Vm.hpp"

#include "Localization/Localization.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <time.h>
#include <xlocale.h>

namespace lim {

using ld::tr;

void ClosureObject::release() {
    cells.clear();
    environment.reset();
}

void Environment::release() {
    values.clear();
    mutableNames.clear();
}

std::optional<Value> Host::global(Vm&, std::string_view) { return std::nullopt; }
Value Host::getMember(Vm&, const Value& object, const std::string& name) { throw ScriptError(tr("«{0}» non è un membro di {1}", name, kindName(object))); }
void Host::setMember(Vm&, const Value& object, const std::string& name, const Value&) { throw ScriptError(tr("«{0}» non si può assegnare su {1}", name, kindName(object))); }
Value Host::connect(Vm&, const Value&, std::function<void(std::vector<Value>)>, bool) { throw ScriptError(tr("Evento non disponibile")); }
std::string Host::describe(const Value& object) { return kindName(object); }
bool Host::isA(const Value& object, std::string_view type) { return kindName(object) == type; }
std::optional<Value> Host::importModule(Vm&, const std::string&) { return std::nullopt; }

const Value& NativeCall::argument(std::size_t i) const {
    static const Value none;
    return i < arguments.size() ? arguments[i] : none;
}

const Value* NativeCall::named(std::string_view name) const {
    if (!names) return nullptr;
    for (std::size_t i = 0; i < names->size() && i < arguments.size(); ++i)
        if ((*names)[i] == name) return &arguments[i];
    return nullptr;
}

const Value& NativeCall::parameter(std::size_t position, std::string_view name) const {
    if (const Value* v = named(name)) return *v;
    if (names) {
        std::size_t positional = 0;
        for (std::size_t i = 0; i < names->size() && i < arguments.size(); ++i) {
            if (!(*names)[i].empty()) continue;
            if (positional == position) return arguments[i];
            ++positional;
        }
        static const Value none;
        return none;
    }
    return argument(position);
}

namespace {

using Clock = std::chrono::steady_clock;

double working() {
    timespec now{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now);
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) / 1e9;
}

locale_t cLocale() {
    static locale_t locale = newlocale(LC_ALL_MASK, "C", nullptr);
    return locale;
}

std::string formatReal(double value) {
    if (std::isnan(value)) return "nan";
    if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
    char buffer[40];
    snprintf_l(buffer, sizeof buffer, cLocale(), "%.14g", value);
    return buffer;
}

bool truthy(const Value& v) {
    if (v.kind == ValueKind::boolean) return v.flag;
    throw ScriptError(tr("Serve un valore vero o falso, trovato {0}", kindName(v)));
}

}

Vm::Vm(Limits limits) : bounds(limits), account(std::make_shared<std::atomic<long long>>(0)) { installLibrary(*this); }

Vm::~Vm() {
    stopAll();
    for (auto& weak : containers)
        if (auto object = weak.lock()) object->release();
    for (auto& weak : environments)
        if (auto environment = weak.lock()) environment->release();
    for (auto& [name, value] : builtins)
        if (value.object) value.object->release();
    builtins.clear();
}

void Vm::defineBuiltin(const std::string& name, Value value) { builtins[name] = std::move(value); }

void Vm::charge(Object& object, long long bytes) {
    if (!object.account) object.account = account;
    object.charged += bytes;
    long long used = account->fetch_add(bytes) + bytes;
    if (used > bounds.memoryBytes) throw ScriptError(tr("Memoria esaurita: gli script hanno superato il limite di {0} MB", std::to_string(bounds.memoryBytes / (1024 * 1024))));
}

void Vm::register_(const std::shared_ptr<Object>& object) {
    containers.push_back(object);
    if (containers.size() > 4096 && containers.size() % 4096 == 0)
        containers.erase(std::remove_if(containers.begin(), containers.end(), [](const std::weak_ptr<Object>& w) { return w.expired(); }), containers.end());
}

Value Vm::text(std::string value) {
    auto t = std::make_shared<TextObject>();
    long long size = static_cast<long long>(value.size()) + 32;
    t->text = std::move(value);
    charge(*t, size);
    return Value::make(ValueKind::text, t);
}

Value Vm::list(std::vector<Value> items) {
    auto l = std::make_shared<ListObject>();
    long long size = static_cast<long long>(items.size()) * 24 + 48;
    l->items = std::move(items);
    charge(*l, size);
    register_(l);
    return Value::make(ValueKind::list, l);
}

Value Vm::map() {
    auto m = std::make_shared<MapObject>();
    charge(*m, 64);
    register_(m);
    return Value::make(ValueKind::map, m);
}

Value Vm::native(std::string name, NativeFunction function, bool async) {
    auto n = std::make_shared<NativeObject>();
    n->name = std::move(name);
    n->function = std::move(function);
    n->async = async;
    return Value::make(ValueKind::native, n);
}

Value Vm::boundNative(std::string name, Value receiver, NativeFunction function, bool async) {
    auto n = std::make_shared<NativeObject>();
    n->name = std::move(name);
    n->function = std::move(function);
    n->receiver = std::move(receiver);
    n->async = async;
    return Value::make(ValueKind::native, n);
}

Value Vm::hostObject(std::uint64_t handle, std::string className, std::string tag) {
    auto h = std::make_shared<HostObject>();
    h->handle = handle;
    h->className = std::move(className);
    h->tag = std::move(tag);
    return Value::make(ValueKind::host, h);
}

std::shared_ptr<TaskObject> Vm::newTask() { return std::make_shared<TaskObject>(); }

const std::string& Vm::textOf(const Value& value) const {
    static const std::string empty;
    return value.kind == ValueKind::text ? value.as<TextObject>()->text : empty;
}

std::shared_ptr<Environment> Vm::newEnvironment(const std::string& name, std::map<std::string, Value> preset) {
    auto environment = std::make_shared<Environment>();
    environment->name = name;
    for (auto& [key, value] : preset) environment->values[key] = std::move(value);
    environments.push_back(environment);
    return environment;
}

void Vm::emit(Severity severity, const std::string& message, const Fiber* fiber) {
    if (!outputSink) return;
    OutputMessage out;
    out.severity = severity;
    out.text = message;
    if (fiber) {
        out.source = fiber->label;
        if (!fiber->frames.empty()) {
            const Frame& frame = fiber->frames.back();
            const Prototype& p = frame.closure->proto();
            std::size_t pc = frame.ip > 0 ? frame.ip - 1 : 0;
            if (pc < p.lines.size()) {
                out.line = p.lines[pc];
                out.column = p.columns[pc];
            }
        }
    }
    outputSink(out);
}

std::string Vm::location(const Fiber& fiber) const {
    if (fiber.frames.empty()) return fiber.label;
    const Frame& frame = fiber.frames.back();
    const Prototype& p = frame.closure->proto();
    std::size_t pc = frame.ip > 0 ? frame.ip - 1 : 0;
    std::string file = frame.closure->module->file.empty() ? fiber.label : frame.closure->module->file;
    if (pc >= p.lines.size()) return file;
    return file + ":" + std::to_string(p.lines[pc]) + ":" + std::to_string(p.columns[pc]);
}

std::string Vm::traceback(const Fiber& fiber) const {
    std::string out;
    for (std::size_t i = fiber.frames.size(); i-- > 0;) {
        const Frame& frame = fiber.frames[i];
        const Prototype& p = frame.closure->proto();
        std::size_t pc = frame.ip > 0 ? frame.ip - 1 : 0;
        out += "  in " + (p.name == "main" ? std::string("script") : p.name);
        if (pc < p.lines.size()) out += " (riga " + std::to_string(p.lines[pc]) + ")";
        out += "\n";
        if (fiber.frames.size() - i > 12) { out += "  …\n"; break; }
    }
    return out;
}

std::string Vm::toText(const Value& v, int depth) {
    if (depth > 8) return "…";
    switch (v.kind) {
    case ValueKind::none: return "none";
    case ValueKind::boolean: return v.flag ? "true" : "false";
    case ValueKind::integer: return std::to_string(v.integer);
    case ValueKind::number: return formatReal(v.number);
    case ValueKind::text: return v.as<TextObject>()->text;
    case ValueKind::list: {
        std::string out = "[";
        const auto& items = v.as<ListObject>()->items;
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i) out += ", ";
            if (i >= 50) { out += "…"; break; }
            out += items[i].kind == ValueKind::text ? "\"" + items[i].as<TextObject>()->text + "\"" : toText(items[i], depth + 1);
        }
        return out + "]";
    }
    case ValueKind::map: {
        const auto& entries = v.as<MapObject>()->entries;
        if (entries.empty()) return "[:]";
        std::string out = "[";
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (i) out += ", ";
            if (i >= 50) { out += "…"; break; }
            out += (entries[i].first.kind == ValueKind::text ? "\"" + entries[i].first.as<TextObject>()->text + "\"" : toText(entries[i].first, depth + 1)) + ": " +
                   (entries[i].second.kind == ValueKind::text ? "\"" + entries[i].second.as<TextObject>()->text + "\"" : toText(entries[i].second, depth + 1));
        }
        return out + "]";
    }
    case ValueKind::function: return "fn " + v.as<ClosureObject>()->proto().name;
    case ValueKind::native: return "fn " + v.as<NativeObject>()->name;
    case ValueKind::boundMethod: return tr("fn metodo");
    case ValueKind::instance: return v.as<InstanceObject>()->type->descriptor.name;
    case ValueKind::classType: return "class " + v.as<ClassObject>()->descriptor.name;
    case ValueKind::enumValue: {
        const auto* e = v.as<EnumValueObject>();
        return e->type.empty() ? "." + e->name : e->type + "." + e->name;
    }
    case ValueKind::enumType: return "enum " + v.as<EnumTypeObject>()->name;
    case ValueKind::data: {
        const auto* d = v.as<DataObject>();
        auto f = [&](int i) { return formatReal(d->values[static_cast<std::size_t>(i)]); };
        switch (d->kind) {
        case DataKind::vec2: return "Vec2(" + f(0) + ", " + f(1) + ")";
        case DataKind::vec3: return "Vec3(" + f(0) + ", " + f(1) + ", " + f(2) + ")";
        case DataKind::color: return "Color(" + f(0) + ", " + f(1) + ", " + f(2) + ")";
        case DataKind::rotation: return "Rotation(" + f(0) + ", " + f(1) + ", " + f(2) + ")";
        case DataKind::transform: return "Transform(Vec3(" + f(0) + ", " + f(1) + ", " + f(2) + "), Rotation(" + f(3) + ", " + f(4) + ", " + f(5) + "))";
        case DataKind::ray: return "Ray(Vec3(" + f(0) + ", " + f(1) + ", " + f(2) + "), Vec3(" + f(3) + ", " + f(4) + ", " + f(5) + "))";
        case DataKind::bounds: return "Bounds(Vec3(" + f(0) + ", " + f(1) + ", " + f(2) + "), Vec3(" + f(3) + ", " + f(4) + ", " + f(5) + "))";
        case DataKind::dim: return "Dim(" + f(0) + ", " + f(1) + ")";
        case DataKind::dim2: return "Dim2(" + f(0) + ", " + f(1) + ", " + f(2) + ", " + f(3) + ")";
        }
        return "Data";
    }
    case ValueKind::host: return hostInterface ? hostInterface->describe(v) : v.as<HostObject>()->className;
    case ValueKind::task: return "Task";
    case ValueKind::range: {
        const auto* r = v.as<RangeObject>();
        return std::to_string(r->low) + (r->inclusive ? "..." : "..<") + std::to_string(r->high);
    }
    case ValueKind::module: return "modulo " + v.as<ModuleObject>()->name;
    default: return kindName(v);
    }
}

std::shared_ptr<Fiber> Vm::run(std::shared_ptr<const Module> module, std::shared_ptr<Environment> environment, const std::string& label) {
    auto fiber = std::make_shared<Fiber>();
    fiber->id = nextFiber++;
    fiber->label = label;
    fiber->task = newTask();
    fiber->task->fiber = fiber;
    auto invalid = module ? validate(*module) : std::optional<std::string>(tr("modulo assente"));
    if (invalid) {
        fail(fiber, tr("Bytecode non valido: {0}", *invalid));
        return fiber;
    }
    const auto& entry = module->prototypes[module->main];
    if (1ULL + entry.locals + entry.maxStack > bounds.maximumStack || running() >= bounds.maximumFibers) {
        fail(fiber, tr("Limite di stack o attività degli script superato"));
        return fiber;
    }
    auto closure = std::make_shared<ClosureObject>();
    closure->module = std::move(module);
    closure->prototype = closure->module->main;
    closure->environment = std::move(environment);
    closure->label = label;
    register_(closure);
    const Prototype& p = closure->proto();
    Frame frame;
    frame.closure = closure;
    frame.environment = closure->environment;
    frame.base = 1;
    frame.callee = 0;
    fiber->stack.push_back(Value::make(ValueKind::function, closure));
    fiber->stack.resize(1 + p.locals);
    fiber->frames.push_back(frame);
    ++stats.fibersCreated;
    fibers.push_back(fiber);
    schedule(fiber);
    return fiber;
}

Value Vm::runModule(std::shared_ptr<const Module> module, std::shared_ptr<Environment> environment, const std::string& label, std::string* error) {
    auto fiber = run(std::move(module), environment, label);
    if (fiber->state == Fiber::State::failed) {
        if (error) *error = fiber->task->error;
        return Value();
    }
    if (fiber->state != Fiber::State::done) {
        if (error) *error = tr("Un modulo non può attendere durante il caricamento");
        fiber->state = Fiber::State::failed;
        return Value();
    }
    auto object = std::make_shared<ModuleObject>();
    object->name = label;
    for (const auto& [name, value] : environment->values) object->exports[name] = value;
    register_(object);
    return Value::make(ValueKind::module, object);
}

std::shared_ptr<Fiber> Vm::dispatch(const Value& function, std::vector<Value> arguments, const std::string& label) {
    if (function.kind == ValueKind::function) {
        const Prototype& p = function.as<ClosureObject>()->proto();
        std::size_t accepted = p.parameters - (p.method ? 1 : 0);
        if (arguments.size() > accepted) arguments.resize(accepted);
    }
    ++eventCount[label];
    return spawn(function, std::move(arguments), label);
}

std::map<std::string, ScriptUsage> Vm::scriptUsage() const {
    std::map<std::string, ScriptUsage> out;
    for (const auto& [label, seconds] : profile) out[label].seconds = seconds;
    for (const auto& [label, count] : eventCount) out[label].events = count;
    for (const auto& [label, count] : frameCount) out[label].frames = count;
    return out;
}

std::shared_ptr<Fiber> Vm::spawn(const Value& function, std::vector<Value> arguments, const std::string& label) {
    auto fiber = std::make_shared<Fiber>();
    fiber->id = nextFiber++;
    fiber->label = label;
    fiber->task = newTask();
    fiber->task->fiber = fiber;
    fiber->stack.push_back(function);
    for (auto& a : arguments) fiber->stack.push_back(std::move(a));
    ++stats.fibersCreated;
    if (fibers.size() >= bounds.maximumFibers) {
        emit(Severity::error, tr("Troppe attività contemporanee: evento ignorato"), fiber.get());
        fiber->state = Fiber::State::failed;
        return fiber;
    }
    std::string error;
    bool suspended = false;
    auto previous = current;
    current = fiber;
    bool ok;
    try {
        ok = prepareCall(*fiber, 0, fiber->stack.size() - 1, nullptr, error, suspended);
    } catch (const ScriptError& e) {
        ok = false;
        error = e.what();
    }
    current = previous;
    if (!ok) {
        fiber->state = Fiber::State::failed;
        emit(Severity::error, label + ": " + error, fiber.get());
        completeTask(fiber->task, Value(), true, error);
        return fiber;
    }
    fibers.push_back(fiber);
    if (fiber->frames.empty() && !suspended) {
        Value result = fiber->stack.empty() ? Value() : fiber->stack.back();
        finish(fiber, result);
        return fiber;
    }
    if (suspended) {
        fiber->state = Fiber::State::waiting;
        return fiber;
    }
    schedule(fiber);
    return fiber;
}

Value Vm::callSync(const Value& function, std::vector<Value> arguments) {
    if (nesting >= bounds.maximumNesting) throw ScriptError(tr("Troppe chiamate annidate tra script e motore"));
    auto fiber = std::make_shared<Fiber>();
    fiber->id = nextFiber++;
    fiber->synchronous = true;
    fiber->label = current ? current->label : std::string("script");
    fiber->stack.push_back(function);
    for (auto& a : arguments) fiber->stack.push_back(std::move(a));
    std::string error;
    bool suspended = false;
    ++nesting;
    auto previous = current;
    current = fiber;
    bool ok;
    try {
        ok = prepareCall(*fiber, 0, fiber->stack.size() - 1, nullptr, error, suspended);
    } catch (...) {
        current = previous;
        --nesting;
        throw;
    }
    if (!ok) {
        current = previous;
        --nesting;
        throw ScriptError(error);
    }
    if (suspended) {
        current = previous;
        --nesting;
        throw ScriptError(tr("Non si può attendere qui: la funzione viene eseguita subito"));
    }
    Outcome outcome = fiber->frames.empty() ? Outcome::finished : execute(fiber);
    current = previous;
    --nesting;
    if (outcome == Outcome::failed) throw ScriptError(fiber->task ? fiber->task->error : std::string("errore"));
    if (outcome == Outcome::suspended) throw ScriptError(tr("Non si può attendere qui: la funzione viene eseguita subito"));
    return fiber->stack.empty() ? Value() : fiber->stack.back();
}

void Vm::schedule(const std::shared_ptr<Fiber>& fiber) {
    if (fiber->state == Fiber::State::done || fiber->state == Fiber::State::failed) return;
    fiber->state = Fiber::State::running;
    if (nesting >= bounds.maximumNesting) {
        fail(fiber, tr("Troppe chiamate annidate tra script e motore"));
        return;
    }
    ++nesting;
    auto previous = current;
    current = fiber;
    Outcome outcome = execute(fiber);
    current = previous;
    --nesting;
    if (outcome == Outcome::suspended) fiber->state = Fiber::State::waiting;
}

void Vm::finish(const std::shared_ptr<Fiber>& fiber, Value result) {
    fiber->state = Fiber::State::done;
    if (fiber->task) completeTask(fiber->task, std::move(result));
}

void Vm::fail(const std::shared_ptr<Fiber>& fiber, const std::string& message) {
    fiber->state = Fiber::State::failed;
    ++stats.errors;
    std::string where = location(*fiber);
    std::string full = where + ": " + message;
    if (!fiber->synchronous) {
        if (outputSink) {
            OutputMessage out;
            out.severity = Severity::error;
            out.text = full;
            out.source = fiber->label;
            if (!fiber->frames.empty()) {
                const Frame& frame = fiber->frames.back();
                const Prototype& p = frame.closure->proto();
                std::size_t pc = frame.ip > 0 ? frame.ip - 1 : 0;
                if (pc < p.lines.size()) { out.line = p.lines[pc]; out.column = p.columns[pc]; }
            }
            out.stack = traceback(*fiber);
            outputSink(out);
        }
    }
    if (fiber->task) completeTask(fiber->task, Value(), true, fiber->synchronous ? message : full);
    fiber->frames.clear();
    fiber->stack.clear();
}

void Vm::completeTask(const std::shared_ptr<TaskObject>& task, Value result, bool failed, std::string error) {
    if (!task || task->done) return;
    task->done = true;
    task->failed = failed;
    task->result = std::move(result);
    task->error = std::move(error);
    auto waiters = std::move(task->waiters);
    task->waiters.clear();
    for (auto& weak : waiters) {
        auto fiber = weak.lock();
        if (!fiber || fiber->state != Fiber::State::waiting) continue;
        fiber->wait = {};
        if (task->failed) {
            fail(fiber, task->cancelled ? tr("Attività annullata") : tr("Un’attività attesa è fallita: {0}", task->error));
            continue;
        }
        fiber->stack.push_back(task->result);
        schedule(fiber);
    }
}

Value Vm::suspend(Suspension suspension, std::function<void(Value)> then) {
    auto task = newTask();
    if (suspension.kind == Suspension::Kind::event) {
        if (!hostInterface) throw ScriptError(tr("Eventi non disponibili"));
        std::weak_ptr<TaskObject> weak = task;
        Vm* self = this;
        hostInterface->connect(*this, suspension.target, [self, weak](std::vector<Value> arguments) {
            auto t = weak.lock();
            if (!t || t->done) return;
            Value result = arguments.empty() ? Value() : arguments.size() == 1 ? arguments[0] : self->list(arguments);
            self->completeTask(t, result);
        }, true);
        return Value::make(ValueKind::task, task);
    }
    if (suspension.kind == Suspension::Kind::task) {
        if (suspension.target.kind == ValueKind::task) return suspension.target;
        return Value::make(ValueKind::task, task);
    }
    Pending p;
    p.task = task;
    p.suspension = std::move(suspension);
    p.started = time;
    p.then = std::move(then);
    if (current) p.label = current->label;
    pending.push_back(std::move(p));
    return Value::make(ValueKind::task, task);
}

void Vm::step(double delta) {
    auto started = Clock::now();
    time += std::max(0.0, delta);
    auto due = std::move(pending);
    pending.clear();
    std::vector<Pending> keep;
    for (auto& p : due) {
        if (p.task->done) continue;
        switch (p.suspension.kind) {
        case Suspension::Kind::time:
            if (time >= p.suspension.until) {
                completeTask(p.task, Value::real(time - p.started));
                if (p.then) p.then(Value::real(time - p.started));
            } else keep.push_back(std::move(p));
            break;
        case Suspension::Kind::frame:
            if (!p.label.empty()) ++frameCount[p.label];
            completeTask(p.task, Value::real(delta));
            if (p.then) p.then(Value::real(delta));
            break;
        case Suspension::Kind::poll: {
            std::optional<Value> found;
            try {
                found = p.suspension.poll ? p.suspension.poll() : std::nullopt;
            } catch (const ScriptError&) {
                found = Value();
            }
            if (found) completeTask(p.task, *found);
            else if (p.suspension.timeout >= 0 && time - p.started >= p.suspension.timeout) completeTask(p.task, Value());
            else keep.push_back(std::move(p));
            break;
        }
        default: break;
        }
    }
    for (auto& p : pending) keep.push_back(std::move(p));
    pending = std::move(keep);
    collectFinished();
    stats.scriptSeconds += std::chrono::duration<double>(Clock::now() - started).count();
}

void Vm::cancelTask(const std::shared_ptr<TaskObject>& task) {
    if (!task || task->done) return;
    if (auto fiber = task->fiber.lock()) {
        fiber->state = Fiber::State::done;
        fiber->frames.clear();
        fiber->stack.clear();
    }
    for (auto& p : pending)
        if (p.task == task) p.then = nullptr;
    task->cancelled = true;
    completeTask(task, Value(), true, tr("Attività annullata"));
}

void Vm::collectFinished() {
    fibers.erase(std::remove_if(fibers.begin(), fibers.end(), [](const std::shared_ptr<Fiber>& f) { return f->state == Fiber::State::done || f->state == Fiber::State::failed; }), fibers.end());
}

std::size_t Vm::running() const {
    std::size_t count = 0;
    for (const auto& f : fibers)
        if (f->state == Fiber::State::waiting || f->state == Fiber::State::running || f->state == Fiber::State::ready) ++count;
    return count;
}

void Vm::stopAll() {
    for (auto& f : fibers) {
        f->state = Fiber::State::done;
        f->frames.clear();
        f->stack.clear();
    }
    fibers.clear();
    pending.clear();
}

namespace {

Value arithmetic(Vm& vm, Op op, const Value& a, const Value& b) {
    if (a.kind == ValueKind::integer && b.kind == ValueKind::integer) {
        long long x = a.integer, y = b.integer, r = 0;
        switch (op) {
        case Op::add: if (!__builtin_add_overflow(x, y, &r)) return Value::from(r); return Value::real(static_cast<double>(x) + static_cast<double>(y));
        case Op::subtract: if (!__builtin_sub_overflow(x, y, &r)) return Value::from(r); return Value::real(static_cast<double>(x) - static_cast<double>(y));
        case Op::multiply: if (!__builtin_mul_overflow(x, y, &r)) return Value::from(r); return Value::real(static_cast<double>(x) * static_cast<double>(y));
        case Op::divide:
            if (y == 0) throw ScriptError(tr("Divisione per zero"));
            return Value::real(static_cast<double>(x) / static_cast<double>(y));
        case Op::modulo: {
            if (y == 0) throw ScriptError(tr("Resto di una divisione per zero"));
            if (x == std::numeric_limits<long long>::min() && y == -1) return Value::from(0);
            long long m = x % y;
            if (m != 0 && ((m < 0) != (y < 0))) m += y;
            return Value::from(m);
        }
        default: break;
        }
    }
    if (a.numeric() && b.numeric()) {
        double x = a.toNumber(), y = b.toNumber();
        switch (op) {
        case Op::add: return Value::real(x + y);
        case Op::subtract: return Value::real(x - y);
        case Op::multiply: return Value::real(x * y);
        case Op::divide: return Value::real(x / y);
        case Op::modulo: return Value::real(y == 0 ? std::nan("") : x - std::floor(x / y) * y);
        default: break;
        }
    }
    if (op == Op::add && a.kind == ValueKind::text && b.kind == ValueKind::text) {
        const std::string& x = a.as<TextObject>()->text;
        const std::string& y = b.as<TextObject>()->text;
        std::string joined;
        joined.reserve(x.size() + y.size());
        joined += x;
        joined += y;
        return vm.text(std::move(joined));
    }
    auto vector = [](const Value& v) { return v.kind == ValueKind::data && (v.as<DataObject>()->kind == DataKind::vec3 || v.as<DataObject>()->kind == DataKind::vec2 || v.as<DataObject>()->kind == DataKind::color); };
    if (vector(a) && vector(b) && a.as<DataObject>()->kind == b.as<DataObject>()->kind && (op == Op::add || op == Op::subtract)) {
        auto out = std::make_shared<DataObject>(*a.as<DataObject>());
        out->account = nullptr;
        out->charged = 0;
        for (int i = 0; i < 3; ++i) out->values[static_cast<std::size_t>(i)] = op == Op::add ? a.as<DataObject>()->values[static_cast<std::size_t>(i)] + b.as<DataObject>()->values[static_cast<std::size_t>(i)] : a.as<DataObject>()->values[static_cast<std::size_t>(i)] - b.as<DataObject>()->values[static_cast<std::size_t>(i)];
        return Value::make(ValueKind::data, out);
    }
    if (vector(a) && b.numeric() && (op == Op::multiply || op == Op::divide)) {
        double s = b.toNumber();
        auto out = std::make_shared<DataObject>(*a.as<DataObject>());
        out->account = nullptr;
        out->charged = 0;
        for (int i = 0; i < 3; ++i) out->values[static_cast<std::size_t>(i)] = op == Op::multiply ? out->values[static_cast<std::size_t>(i)] * s : out->values[static_cast<std::size_t>(i)] / s;
        return Value::make(ValueKind::data, out);
    }
    if (a.numeric() && vector(b) && op == Op::multiply) return arithmetic(vm, op, b, a);
    const char* symbol = op == Op::add ? "+" : op == Op::subtract ? "-" : op == Op::multiply ? "*" : op == Op::divide ? "/" : "%";
    if (op == Op::add && (a.kind == ValueKind::text || b.kind == ValueKind::text))
        throw ScriptError(tr("Non si può unire {0} e {1}", kindName(a), kindName(b)) + tr(" con +: usa l’interpolazione \"\\(valore)\""));
    throw ScriptError(std::string(tr("Operatore «")) + tr("{0}» non applicabile a {1} e {2}", symbol, kindName(a), kindName(b)));
}

bool compare(Op op, const Value& a, const Value& b) {
    int order;
    if (a.numeric() && b.numeric()) {
        if (a.kind == ValueKind::integer && b.kind == ValueKind::integer) order = a.integer < b.integer ? -1 : a.integer > b.integer ? 1 : 0;
        else {
            double x = a.toNumber(), y = b.toNumber();
            if (std::isnan(x) || std::isnan(y)) return false;
            order = x < y ? -1 : x > y ? 1 : 0;
        }
    } else if (a.kind == ValueKind::text && b.kind == ValueKind::text) {
        int c = a.as<TextObject>()->text.compare(b.as<TextObject>()->text);
        order = c < 0 ? -1 : c > 0 ? 1 : 0;
    } else throw ScriptError(tr("Si possono confrontare solo numeri o testi, non {0} e {1}", kindName(a), kindName(b)));
    switch (op) {
    case Op::less: return order < 0;
    case Op::lessEqual: return order <= 0;
    case Op::greater: return order > 0;
    default: return order >= 0;
    }
}

}

bool Vm::prepareCall(Fiber& fiber, std::size_t callee, std::size_t count, const std::vector<std::string>* names, std::string& error, bool& suspended) {
    Value function = fiber.stack[callee];
    switch (function.kind) {
    case ValueKind::boundMethod: {
        auto* bound = function.as<BoundMethodObject>();
        fiber.stack[callee] = bound->function;
        fiber.stack.insert(fiber.stack.begin() + static_cast<long>(callee) + 1, bound->receiver);
        std::vector<std::string> shifted;
        if (names) {
            shifted.push_back("");
            shifted.insert(shifted.end(), names->begin(), names->end());
        }
        return prepareCall(fiber, callee, count + 1, names ? &shifted : nullptr, error, suspended);
    }
    case ValueKind::function: {
        auto closure = std::static_pointer_cast<ClosureObject>(function.object);
        const Prototype& p = closure->proto();
        if (fiber.frames.size() >= bounds.maximumFrames) {
            error = tr("Troppe chiamate annidate (ricorsione infinita?)");
            return false;
        }
        std::vector<Value> arguments(fiber.stack.begin() + static_cast<long>(callee) + 1, fiber.stack.begin() + static_cast<long>(callee + 1 + count));
        std::vector<Value> ordered(p.parameters);
        std::vector<bool> filled(p.parameters, false);
        std::size_t positional = 0;
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            std::size_t slot = positional;
            if (names && i < names->size() && !(*names)[i].empty()) {
                auto it = std::find(p.parameterNames.begin(), p.parameterNames.end(), (*names)[i]);
                if (it == p.parameterNames.end()) {
                    error = tr("Parametro «{0}» sconosciuto per {1}", (*names)[i], p.name);
                    return false;
                }
                slot = static_cast<std::size_t>(it - p.parameterNames.begin());
            } else ++positional;
            if (slot >= p.parameters) {
                error = tr("Troppi argomenti per {0}: ne servono al massimo {1}{2}", p.name, std::to_string(p.parameters), (p.method ? " (oltre a self)" : ""));
                return false;
            }
            ordered[slot] = arguments[i];
            filled[slot] = true;
        }
        for (std::size_t i = 0; i < p.parameters; ++i) {
            if (filled[i]) continue;
            if (!p.defaults.empty() && p.defaults[i] != std::numeric_limits<std::uint32_t>::max()) {
                auto thunk = std::make_shared<ClosureObject>();
                thunk->module = closure->module;
                thunk->prototype = p.defaults[i];
                thunk->environment = closure->environment;
                thunk->cells = closure->cells;
                ordered[i] = callSync(Value::make(ValueKind::function, thunk), {});
                filled[i] = true;
            } else if (i < p.minimum) {
                error = tr("Argomento mancante «{0}» per {1}", p.parameterNames[i], p.name);
                return false;
            }
        }
        if (p.async && !fiber.synchronous && !(fiber.frames.empty() && callee == 0 && fiber.task)) {
            auto child = std::make_shared<Fiber>();
            child->id = nextFiber++;
            child->label = fiber.label;
            child->task = newTask();
            child->task->fiber = child;
            child->stack.push_back(function);
            for (auto& a : ordered) child->stack.push_back(a);
            fiber.stack.resize(callee);
            fiber.stack.push_back(Value::make(ValueKind::task, child->task));
            Frame frame;
            frame.closure = closure;
            frame.environment = closure->environment;
            frame.base = 1;
            frame.callee = 0;
            child->stack.resize(1 + p.locals);
            for (std::size_t i = 0; i < p.parameters; ++i) {
                Value v = ordered[i];
                if (!p.cellParameters.empty() && p.cellParameters[i]) {
                    auto cell = std::make_shared<CellObject>();
                    cell->value = v;
                    register_(cell);
                    v = Value::make(ValueKind::cell, cell);
                }
                child->stack[1 + i] = v;
            }
            child->frames.push_back(frame);
            ++stats.fibersCreated;
            fibers.push_back(child);
            schedule(child);
            return true;
        }
        fiber.stack.resize(callee + 1);
        Frame frame;
        frame.closure = closure;
        frame.environment = closure->environment;
        frame.base = callee + 1;
        frame.callee = callee;
        if (fiber.stack.size() + p.locals + p.maxStack > bounds.maximumStack) {
            error = tr("Stack degli script esaurito");
            return false;
        }
        fiber.stack.resize(frame.base + p.locals);
        for (std::size_t i = 0; i < p.parameters; ++i) {
            Value v = ordered[i];
            if (!p.cellParameters.empty() && p.cellParameters[i]) {
                auto cell = std::make_shared<CellObject>();
                cell->value = v;
                register_(cell);
                v = Value::make(ValueKind::cell, cell);
            }
            fiber.stack[frame.base + i] = v;
        }
        fiber.frames.push_back(frame);
        return true;
    }
    case ValueKind::native: {
        auto* native = function.as<NativeObject>();
        std::vector<Value> arguments;
        arguments.reserve(count + 1);
        if (!native->receiver.isNone()) arguments.push_back(native->receiver);
        for (std::size_t i = 0; i < count; ++i) arguments.push_back(fiber.stack[callee + 1 + i]);
        std::vector<std::string> shifted;
        const std::vector<std::string>* effective = names;
        if (names && !native->receiver.isNone()) {
            shifted.push_back("");
            shifted.insert(shifted.end(), names->begin(), names->end());
            effective = &shifted;
        }
        NativeCall call{*this, std::span<const Value>(arguments), effective, &fiber};
        NativeResult result;
        try {
            result = native->function(call);
        } catch (const ScriptError& e) {
            error = e.what();
            return false;
        }
        fiber.stack.resize(callee);
        if (result.suspension.kind != Suspension::Kind::none) {
            fiber.stack.push_back(suspend(std::move(result.suspension)));
            return true;
        }
        fiber.stack.push_back(std::move(result.value));
        return true;
    }
    case ValueKind::classType: {
        auto type = std::static_pointer_cast<ClassObject>(function.object);
        auto instance = std::make_shared<InstanceObject>();
        instance->type = type;
        instance->fields.resize(type->descriptor.fields.size());
        charge(*instance, static_cast<long long>(instance->fields.size()) * 24 + 48);
        register_(instance);
        Value self = Value::make(ValueKind::instance, instance);
        if (type->descriptor.hasFieldInitializer) {
            auto initializer = std::make_shared<ClosureObject>();
            initializer->module = type->module;
            initializer->prototype = type->descriptor.fieldInitializer;
            initializer->environment = fiber.frames.empty() ? nullptr : fiber.frames.back().environment;
            callSync(Value::make(ValueKind::function, initializer), {self});
        }
        auto init = type->methodIndex.find("init");
        if (init != type->methodIndex.end()) {
            auto method = std::make_shared<ClosureObject>();
            method->module = type->module;
            method->prototype = init->second;
            method->environment = fiber.frames.empty() ? nullptr : fiber.frames.back().environment;
            fiber.stack[callee] = Value::make(ValueKind::function, method);
            fiber.stack.insert(fiber.stack.begin() + static_cast<long>(callee) + 1, self);
            std::vector<std::string> shifted;
            if (names) {
                shifted.push_back("");
                shifted.insert(shifted.end(), names->begin(), names->end());
            }
            return prepareCall(fiber, callee, count + 1, names ? &shifted : nullptr, error, suspended);
        }
        for (std::size_t i = 0; i < count; ++i) {
            if (!names || i >= names->size() || (*names)[i].empty()) {
                error = tr("Per creare {0} indica i campi per nome", type->descriptor.name);
                return false;
            }
            auto field = type->fieldIndex.find((*names)[i]);
            if (field == type->fieldIndex.end()) {
                error = tr("«{0}» non è un campo di {1}", (*names)[i], type->descriptor.name);
                return false;
            }
            instance->fields[field->second] = fiber.stack[callee + 1 + i];
        }
        fiber.stack.resize(callee);
        fiber.stack.push_back(self);
        return true;
    }
    case ValueKind::none: error = "Impossibile chiamare none"; return false;
    default: error = tr("Un valore {0} non si può chiamare", kindName(function)); return false;
    }
}

void Vm::setDebugger(DebugSettings settings) { debugState = std::make_shared<DebugSettings>(std::move(settings)); }

void Vm::clearDebugger() {
    debugState.reset();
    stepMode = StepMode::run;
    if (auto fiber = std::move(pausedFiber)) schedule(fiber);
}

void Vm::resume(StepMode mode) {
    auto fiber = std::move(pausedFiber);
    pausedFiber.reset();
    if (!fiber || fiber->state == Fiber::State::done || fiber->state == Fiber::State::failed) return;
    stepMode = mode;
    stepFiber = fiber->id;
    stepDepth = fiber->frames.size();
    schedule(fiber);
}

DebugStop Vm::inspect(const Fiber& fiber) const {
    DebugStop stop;
    stop.fiber = fiber.id;
    for (std::size_t i = fiber.frames.size(); i-- > 0;) {
        const Frame& frame = fiber.frames[i];
        const Prototype& p = frame.closure->proto();
        DebugFrame out;
        out.function = p.name.empty() ? "principale" : p.name;
        out.file = frame.closure->module->file.empty() ? fiber.label : frame.closure->module->file;
        std::size_t pc = i + 1 == fiber.frames.size() ? frame.ip : (frame.ip > 0 ? frame.ip - 1 : 0);
        if (pc < p.lines.size()) out.line = p.lines[pc];
        std::map<std::uint32_t, std::string> names;
        for (std::uint32_t slot = 0; slot < p.parameterNames.size(); ++slot) names[slot] = p.parameterNames[slot];
        for (const auto& l : p.localNames)
            if (l.from <= pc) names[l.slot] = l.name;
        auto* self = const_cast<Vm*>(this);
        for (const auto& [slot, name] : names) {
            std::size_t index = frame.base + slot;
            if (index >= fiber.stack.size()) continue;
            Value v = fiber.stack[index];
            if (v.kind == ValueKind::cell) v = v.as<CellObject>()->value;
            std::string text = self->toText(v);
            if (text.size() > 200) text = text.substr(0, 197) + "...";
            out.locals.push_back({name, text});
        }
        if (frame.environment) {
            std::map<std::string, Value> globals(frame.environment->values.begin(), frame.environment->values.end());
            for (const auto& global : globals) {
                const auto& name = global.first;
                const auto& value = global.second;
                if (std::any_of(out.locals.begin(), out.locals.end(), [&](const auto& local) { return local.name == name; })) continue;
                std::string text = self->toText(value);
                if (text.size() > 200) text = text.substr(0, 197) + "...";
                out.locals.push_back({name, text});
            }
        }
        stop.frames.push_back(std::move(out));
    }
    return stop;
}

bool Vm::debugStop(const std::shared_ptr<Fiber>& fiberPointer) {
    Fiber& fiber = *fiberPointer;
    const Frame& frame = fiber.frames.back();
    const Prototype& p = frame.closure->proto();
    std::uint32_t line = frame.ip < p.lines.size() ? p.lines[frame.ip] : 0;
    std::size_t depth = fiber.frames.size();
    if (line == fiber.lastLine && depth == fiber.lastDepth) return false;
    bool lineChanged = line != fiber.lastLine;
    fiber.lastLine = line;
    fiber.lastDepth = depth;
    std::string reason;
    if (stepMode != StepMode::run && stepFiber == fiber.id) {
        bool stop = stepMode == StepMode::into || (stepMode == StepMode::over && depth <= stepDepth) || (stepMode == StepMode::out && depth < stepDepth);
        if (stop) reason = "passo";
    }
    if (reason.empty() && lineChanged) {
        const std::string& file = frame.closure->module->file.empty() ? fiber.label : frame.closure->module->file;
        auto it = debugState->breakpoints.find(file);
        if (it != debugState->breakpoints.end() && it->second.count(line)) reason = tr("punto di interruzione");
    }
    if (reason.empty()) return false;
    stepMode = StepMode::run;
    pausedFiber = fiberPointer;
    fiber.wait = {};
    DebugStop stop = inspect(fiber);
    stop.reason = reason;
    if (debugState->stopped) debugState->stopped(stop);
    return true;
}

Vm::Outcome Vm::execute(const std::shared_ptr<Fiber>& fiberPointer) {
    Fiber& fiber = *fiberPointer;
    auto started = Clock::now();
    double began = working();
    std::uint64_t executed = 0;
    std::string failure;
    while (!fiber.frames.empty()) {
        Frame* frame = &fiber.frames.back();
        const Prototype* p = &frame->closure->proto();
        if (frame->ip >= p->code.size()) {
            failure = tr("Esecuzione oltre la fine della funzione");
            break;
        }
        if (debugState && !fiber.synchronous && debugStop(fiberPointer)) {
            profile[fiber.label] += std::chrono::duration<double>(Clock::now() - started).count();
            stats.instructions += executed;
            return Outcome::suspended;
        }
        const Instruction ins = p->code[frame->ip++];
        ++executed;
        if (executed > bounds.instructionsPerSlice || (executed & 0x3FFF) == 0) {
            if (executed > bounds.instructionsPerSlice || working() - began > bounds.sliceSeconds) {
                failure = tr("Tempo di esecuzione esaurito: lo script non si è fermato (ciclo infinito?). Usa «await Task.nextFrame()» nei cicli lunghi.");
                break;
            }
        }
        try {
            auto& stack = fiber.stack;
            auto pop = [&]() {
                Value v = std::move(stack.back());
                stack.pop_back();
                return v;
            };
            switch (ins.op) {
            case Op::nop: break;
            case Op::constant: {
                const Constant& c = p->constants[ins.a];
                switch (c.kind) {
                case Constant::Kind::integer: stack.push_back(Value::from(c.integer)); break;
                case Constant::Kind::number: stack.push_back(Value::real(c.number)); break;
                case Constant::Kind::text: stack.push_back(text(c.text)); break;
                case Constant::Kind::enumValue: stack.push_back(makeEnum(c.enumValue.type, c.enumValue.name)); break;
                case Constant::Kind::names: stack.push_back(Value()); break;
                }
                break;
            }
            case Op::none: stack.push_back(Value()); break;
            case Op::pushTrue: stack.push_back(Value::boolean(true)); break;
            case Op::pushFalse: stack.push_back(Value::boolean(false)); break;
            case Op::pop: stack.pop_back(); break;
            case Op::dup: stack.push_back(stack.back()); break;
            case Op::dup2: {
                Value a = stack[stack.size() - 2], b = stack.back();
                stack.push_back(a);
                stack.push_back(b);
                break;
            }
            case Op::getLocal: stack.push_back(stack[frame->base + ins.a]); break;
            case Op::setLocal: stack[frame->base + ins.a] = pop(); break;
            case Op::makeCell: {
                auto cell = std::make_shared<CellObject>();
                cell->value = pop();
                register_(cell);
                stack[frame->base + ins.a] = Value::make(ValueKind::cell, cell);
                break;
            }
            case Op::getCell: {
                const Value& slot = stack[frame->base + ins.a];
                stack.push_back(slot.kind == ValueKind::cell ? slot.as<CellObject>()->value : slot);
                break;
            }
            case Op::setCell: {
                Value v = pop();
                Value& slot = stack[frame->base + ins.a];
                if (slot.kind == ValueKind::cell) slot.as<CellObject>()->value = std::move(v);
                else slot = std::move(v);
                break;
            }
            case Op::getUpvalue:
                if (ins.a >= frame->closure->cells.size() || !frame->closure->cells[ins.a]) throw ScriptError(tr("Bytecode non valido: cattura non disponibile"));
                stack.push_back(frame->closure->cells[ins.a]->value);
                break;
            case Op::setUpvalue:
                if (ins.a >= frame->closure->cells.size() || !frame->closure->cells[ins.a]) throw ScriptError(tr("Bytecode non valido: cattura non disponibile"));
                frame->closure->cells[ins.a]->value = pop();
                break;
            case Op::getGlobal: {
                const std::string& name = p->constants[ins.a].text;
                if (frame->environment) {
                    auto it = frame->environment->values.find(name);
                    if (it != frame->environment->values.end()) { stack.push_back(it->second); break; }
                }
                if (auto it = builtins.find(name); it != builtins.end()) { stack.push_back(it->second); break; }
                if (hostInterface) {
                    if (auto v = hostInterface->global(*this, name)) { stack.push_back(*v); break; }
                }
                throw ScriptError(tr("Nome sconosciuto «{0}»", name));
            }
            case Op::setGlobal: {
                const std::string& name = p->constants[ins.a].text;
                if (!frame->environment || !frame->environment->values.count(name)) throw ScriptError(tr("Nome sconosciuto «{0}»", name));
                if (!frame->environment->mutableNames[name]) throw ScriptError(tr("«{0}» è una costante e non si può modificare", name));
                frame->environment->values[name] = pop();
                break;
            }
            case Op::defineGlobal: {
                const std::string& name = p->constants[ins.a].text;
                if (!frame->environment) throw ScriptError(tr("Ambiente dello script mancante"));
                frame->environment->values[name] = pop();
                frame->environment->mutableNames[name] = ins.b != 0;
                break;
            }
            case Op::getMember: {
                Value object = pop();
                stack.push_back(getMember(object, p->constants[ins.a].text));
                break;
            }
            case Op::getMemberOptional: {
                Value object = pop();
                stack.push_back(object.isNone() ? Value() : getMember(object, p->constants[ins.a].text));
                break;
            }
            case Op::setMember: {
                Value value = pop();
                Value object = pop();
                setMember(object, p->constants[ins.a].text, value);
                break;
            }
            case Op::getIndex:
            case Op::getIndexOptional: {
                Value key = pop();
                Value object = pop();
                if (object.isNone() && ins.op == Op::getIndexOptional) { stack.push_back(Value()); break; }
                if (object.kind == ValueKind::list) {
                    if (key.kind != ValueKind::integer) throw ScriptError(tr("L’indice di una lista deve essere un intero, trovato {0}", kindName(key)));
                    const auto& items = object.as<ListObject>()->items;
                    if (key.integer < 0 || static_cast<std::size_t>(key.integer) >= items.size())
                        throw ScriptError(tr("Indice {0} fuori dalla lista di {1} elementi (il primo è 0)", std::to_string(key.integer), std::to_string(items.size())));
                    stack.push_back(items[static_cast<std::size_t>(key.integer)]);
                } else if (object.kind == ValueKind::map) {
                    const Value* found = object.as<MapObject>()->find(key);
                    stack.push_back(found ? *found : Value());
                } else if (object.kind == ValueKind::text) {
                    if (key.kind != ValueKind::integer) throw ScriptError(tr("L’indice di un testo deve essere un intero"));
                    std::vector<std::uint32_t> codes;
                    const std::string& t = object.as<TextObject>()->text;
                    std::size_t i = 0, count = 0, start = std::string::npos, end = t.size();
                    for (; i <= t.size(); ++i) {
                        if (i == t.size() || (static_cast<unsigned char>(t[i]) & 0xC0) != 0x80) {
                            if (count == static_cast<std::size_t>(key.integer) + 1) { end = i; break; }
                            if (count == static_cast<std::size_t>(key.integer)) start = i;
                            ++count;
                        }
                    }
                    if (key.integer < 0 || start == std::string::npos || start >= t.size()) throw ScriptError(tr("Indice {0} fuori dal testo", std::to_string(key.integer)));
                    stack.push_back(text(t.substr(start, end - start)));
                } else if (object.isNone()) throw ScriptError("Impossibile indicizzare none");
                else throw ScriptError(tr("{0} non si può indicizzare con []", kindName(object)));
                break;
            }
            case Op::setIndex: {
                Value value = pop();
                Value key = pop();
                Value object = pop();
                if (object.kind == ValueKind::list) {
                    if (key.kind != ValueKind::integer) throw ScriptError(tr("L’indice di una lista deve essere un intero"));
                    auto& items = object.as<ListObject>()->items;
                    if (key.integer < 0 || static_cast<std::size_t>(key.integer) >= items.size())
                        throw ScriptError(tr("Indice {0} fuori dalla lista di {1} elementi; per aggiungere usa append()", std::to_string(key.integer), std::to_string(items.size())));
                    items[static_cast<std::size_t>(key.integer)] = value;
                } else if (object.kind == ValueKind::map) {
                    auto* m = object.as<MapObject>();
                    if (value.isNone()) m->remove(key);
                    else {
                        bool added = !m->find(key);
                        m->set(key, value);
                        if (added) charge(*m, 48);
                    }
                } else throw ScriptError(tr("{0} non si può modificare con []", kindName(object)));
                break;
            }
            case Op::call:
            case Op::callNamed: {
                std::size_t count = ins.a;
                std::size_t callee = stack.size() - count - 1;
                const std::vector<std::string>* names = ins.op == Op::callNamed ? &p->constants[ins.b].names : nullptr;
                std::string error;
                bool suspended = false;
                if (!prepareCall(fiber, callee, count, names, error, suspended)) throw ScriptError(error);
                break;
            }
            case Op::add:
            case Op::subtract:
            case Op::multiply:
            case Op::divide:
            case Op::modulo: {
                Value b = pop();
                Value a = pop();
                if (a.isNone() || b.isNone()) throw ScriptError(tr("Operazione aritmetica con none"));
                stack.push_back(arithmetic(*this, ins.op, a, b));
                break;
            }
            case Op::equal:
            case Op::notEqual: {
                Value b = pop();
                Value a = pop();
                bool same = valuesEqual(a, b);
                stack.push_back(Value::boolean(ins.op == Op::equal ? same : !same));
                break;
            }
            case Op::less:
            case Op::lessEqual:
            case Op::greater:
            case Op::greaterEqual: {
                Value b = pop();
                Value a = pop();
                stack.push_back(Value::boolean(compare(ins.op, a, b)));
                break;
            }
            case Op::negate: {
                Value a = pop();
                if (a.kind == ValueKind::integer) stack.push_back(a.integer == std::numeric_limits<long long>::min() ? Value::real(-static_cast<double>(a.integer)) : Value::from(-a.integer));
                else if (a.kind == ValueKind::number) stack.push_back(Value::real(-a.number));
                else if (a.kind == ValueKind::data) stack.push_back(arithmetic(*this, Op::multiply, a, Value::real(-1)));
                else throw ScriptError(tr("Il segno meno richiede un numero, trovato {0}", kindName(a)));
                break;
            }
            case Op::logicalNot: {
                Value a = pop();
                stack.push_back(Value::boolean(!truthy(a)));
                break;
            }
            case Op::jump: frame->ip = ins.a; break;
            case Op::jumpIfFalse: if (!truthy(pop())) frame->ip = ins.a; break;
            case Op::jumpIfTrue: if (truthy(pop())) frame->ip = ins.a; break;
            case Op::jumpIfFalseKeep: if (!truthy(stack.back())) frame->ip = ins.a; break;
            case Op::jumpIfTrueKeep: if (truthy(stack.back())) frame->ip = ins.a; break;
            case Op::jumpIfNotNoneKeep: if (!stack.back().isNone()) frame->ip = ins.a; break;
            case Op::jumpIfNoneKeep: if (stack.back().isNone()) frame->ip = ins.a; break;
            case Op::makeList: {
                std::vector<Value> items(stack.end() - static_cast<long>(ins.a), stack.end());
                stack.resize(stack.size() - ins.a);
                stack.push_back(list(std::move(items)));
                break;
            }
            case Op::makeMap: {
                Value m = map();
                auto* object = m.as<MapObject>();
                std::size_t start = stack.size() - ins.a * 2;
                for (std::size_t i = start; i < stack.size(); i += 2) {
                    if (stack[i].isNone()) throw ScriptError(tr("Una chiave di mappa non può essere none"));
                    object->set(stack[i], stack[i + 1]);
                }
                charge(*object, static_cast<long long>(ins.a) * 48);
                stack.resize(start);
                stack.push_back(m);
                break;
            }
            case Op::makeRange: {
                Value high = pop();
                Value low = pop();
                if (low.kind != ValueKind::integer || high.kind != ValueKind::integer) throw ScriptError(tr("Gli estremi di un intervallo devono essere interi"));
                auto range = std::make_shared<RangeObject>();
                range->low = low.integer;
                range->high = high.integer;
                range->inclusive = ins.a != 0;
                stack.push_back(Value::make(ValueKind::range, range));
                break;
            }
            case Op::iterStart: {
                Value source = pop();
                auto iterator = std::make_shared<IteratorObject>();
                iterator->source = source;
                switch (source.kind) {
                case ValueKind::list:
                case ValueKind::map: break;
                case ValueKind::range: iterator->current = source.as<RangeObject>()->low; break;
                case ValueKind::text: {
                    const std::string& t = source.as<TextObject>()->text;
                    for (std::size_t i = 0; i < t.size();) {
                        std::size_t start = i++;
                        while (i < t.size() && (static_cast<unsigned char>(t[i]) & 0xC0) == 0x80) ++i;
                        iterator->characters.push_back(static_cast<std::uint32_t>(start));
                    }
                    iterator->characters.push_back(static_cast<std::uint32_t>(t.size()));
                    break;
                }
                case ValueKind::none: throw ScriptError(tr("Impossibile scorrere none con for"));
                default: throw ScriptError(tr("{0} non si può scorrere con for", kindName(source)));
                }
                stack.push_back(Value::make(ValueKind::iterator, iterator));
                break;
            }
            case Op::iterNext: {
                if (stack.back().kind != ValueKind::iterator) throw ScriptError(tr("Bytecode non valido: iteratore richiesto"));
                auto* iterator = stack.back().as<IteratorObject>();
                const Value& source = iterator->source;
                bool two = ins.b == 2;
                bool done = false;
                switch (source.kind) {
                case ValueKind::list: {
                    const auto& items = source.as<ListObject>()->items;
                    if (iterator->position >= items.size()) { done = true; break; }
                    if (two) stack.push_back(Value::from(static_cast<long long>(iterator->position)));
                    stack.push_back(items[iterator->position]);
                    ++iterator->position;
                    break;
                }
                case ValueKind::map: {
                    const auto& entries = source.as<MapObject>()->entries;
                    if (iterator->position >= entries.size()) { done = true; break; }
                    stack.push_back(entries[iterator->position].first);
                    if (two) stack.push_back(entries[iterator->position].second);
                    ++iterator->position;
                    break;
                }
                case ValueKind::range: {
                    const auto* range = source.as<RangeObject>();
                    bool inside = range->inclusive ? iterator->current <= range->high : iterator->current < range->high;
                    if (!inside || iterator->exhausted) { done = true; break; }
                    if (two) stack.push_back(Value::from(static_cast<long long>(iterator->position)));
                    stack.push_back(Value::from(iterator->current));
                    if (iterator->current == std::numeric_limits<long long>::max()) iterator->exhausted = true;
                    else ++iterator->current;
                    ++iterator->position;
                    break;
                }
                case ValueKind::text: {
                    if (iterator->position + 1 >= iterator->characters.size()) { done = true; break; }
                    const std::string& t = source.as<TextObject>()->text;
                    std::uint32_t start = iterator->characters[iterator->position], end = iterator->characters[iterator->position + 1];
                    if (two) stack.push_back(Value::from(static_cast<long long>(iterator->position)));
                    stack.push_back(text(t.substr(start, end - start)));
                    ++iterator->position;
                    break;
                }
                default: done = true;
                }
                if (done) frame->ip = ins.a;
                break;
            }
            case Op::interpolate: {
                std::string out;
                for (std::size_t i = stack.size() - ins.a; i < stack.size(); ++i) out += toText(stack[i]);
                stack.resize(stack.size() - ins.a);
                stack.push_back(text(std::move(out)));
                break;
            }
            case Op::closure: {
                auto closure = std::make_shared<ClosureObject>();
                closure->module = frame->closure->module;
                closure->prototype = ins.a;
                closure->environment = frame->environment;
                closure->label = frame->closure->label;
                for (const auto& capture : closure->proto().captures) {
                    if (capture.fromLocal) {
                        Value& slot = stack[frame->base + capture.index];
                        if (slot.kind != ValueKind::cell) {
                            auto cell = std::make_shared<CellObject>();
                            cell->value = slot;
                            register_(cell);
                            slot = Value::make(ValueKind::cell, cell);
                        }
                        closure->cells.push_back(std::static_pointer_cast<CellObject>(slot.object));
                    } else {
                        if (capture.index >= frame->closure->cells.size() || !frame->closure->cells[capture.index]) throw ScriptError(tr("Bytecode non valido: cattura non disponibile"));
                        closure->cells.push_back(frame->closure->cells[capture.index]);
                    }
                }
                register_(closure);
                stack.push_back(Value::make(ValueKind::function, closure));
                break;
            }
            case Op::returnValue:
            case Op::returnNone: {
                Value result = ins.op == Op::returnValue ? pop() : Value();
                std::size_t callee = frame->callee;
                fiber.frames.pop_back();
                stack.resize(callee);
                if (fiber.frames.empty()) {
                    stack.push_back(result);
                    profile[fiber.label] += std::chrono::duration<double>(Clock::now() - started).count();
                    stats.instructions += executed;
                    finish(fiberPointer, result);
                    return Outcome::finished;
                }
                stack.push_back(std::move(result));
                break;
            }
            case Op::await: {
                Value awaited = pop();
                if (awaited.kind == ValueKind::host && hostInterface && awaited.as<HostObject>()->tag == "event") {
                    Suspension s;
                    s.kind = Suspension::Kind::event;
                    s.target = awaited;
                    awaited = suspend(std::move(s));
                }
                if (awaited.kind != ValueKind::task) {
                    stack.push_back(awaited);
                    break;
                }
                auto task = std::static_pointer_cast<TaskObject>(awaited.object);
                if (task->done) {
                    if (task->failed) throw ScriptError(task->cancelled ? tr("Attività annullata") : tr("Un’attività attesa è fallita: {0}", task->error));
                    stack.push_back(task->result);
                    break;
                }
                if (fiber.synchronous) throw ScriptError(tr("Non si può attendere qui: la funzione viene eseguita subito"));
                task->waiters.push_back(fiberPointer);
                fiber.wait.kind = Suspension::Kind::task;
                fiber.wait.target = awaited;
                profile[fiber.label] += std::chrono::duration<double>(Clock::now() - started).count();
                stats.instructions += executed;
                return Outcome::suspended;
            }
            case Op::connect: {
                Value function = pop();
                Value event = pop();
                if (!hostInterface) throw ScriptError(tr("Eventi non disponibili"));
                if (event.kind != ValueKind::host || event.as<HostObject>()->tag != "event") throw ScriptError(tr("Dopo «on» serve un evento, trovato {0}", kindName(event)));
                Vm* self = this;
                std::string label = fiber.label;
                Value connection = hostInterface->connect(*this, event, [self, function, label](std::vector<Value> arguments) {
                    ++self->stats.events;
                    self->dispatch(function, std::move(arguments), label);
                }, false);
                stack.push_back(connection);
                break;
            }
            case Op::import: {
                const std::string& name = p->constants[ins.a].text;
                if (!hostInterface) throw ScriptError(tr("Modulo «{0}» non trovato", name));
                auto module = hostInterface->importModule(*this, name);
                if (!module) throw ScriptError(tr("Modulo «{0}» non trovato", name));
                stack.push_back(*module);
                break;
            }
            case Op::makeClass: {
                auto type = std::make_shared<ClassObject>();
                type->module = frame->closure->module;
                type->descriptor = p->classes[ins.a];
                for (std::size_t i = 0; i < type->descriptor.fields.size(); ++i) type->fieldIndex[type->descriptor.fields[i]] = i;
                for (const auto& [name, proto] : type->descriptor.methods) type->methodIndex[name] = proto;
                stack.push_back(Value::make(ValueKind::classType, type));
                break;
            }
            case Op::makeEnum: {
                auto type = std::make_shared<EnumTypeObject>();
                type->name = p->enums[ins.a].name;
                type->cases = p->enums[ins.a].cases;
                stack.push_back(Value::make(ValueKind::enumType, type));
                break;
            }
            case Op::isType: {
                Value v = pop();
                const std::string& type = p->constants[ins.a].text;
                bool result;
                if (type == "Any") result = true;
                else if (type == "Number") result = v.numeric();
                else if (type == "List") result = v.kind == ValueKind::list;
                else if (type == "Map") result = v.kind == ValueKind::map;
                else if (type == "Function") result = v.kind == ValueKind::function || v.kind == ValueKind::native || v.kind == ValueKind::boundMethod;
                else if (v.kind == ValueKind::host && hostInterface) result = hostInterface->isA(v, type);
                else result = kindName(v) == type;
                stack.push_back(Value::boolean(result));
                break;
            }
            case Op::shorthand: stack.push_back(makeEnum("", p->constants[ins.a].text)); break;
            case Op::cast: {
                std::string type = p->constants[ins.a].text;
                const Value& v = stack.back();
                bool optional = !type.empty() && type.back() == '?';
                if (optional) type.pop_back();
                bool result;
                if (type == "Any" || (optional && v.isNone())) result = true;
                else if (type == "Number") result = v.numeric();
                else if (type == "Integer") result = v.kind == ValueKind::integer;
                else if (type == "Text") result = v.kind == ValueKind::text;
                else if (type == "Bool") result = v.kind == ValueKind::boolean;
                else if (type == "List") result = v.kind == ValueKind::list;
                else if (type == "Map") result = v.kind == ValueKind::map;
                else if (type == "Function") result = v.kind == ValueKind::function || v.kind == ValueKind::native || v.kind == ValueKind::boundMethod;
                else if (v.kind == ValueKind::host && hostInterface) result = hostInterface->isA(v, type);
                else result = kindName(v) == type;
                if (!result) throw ScriptError(tr("Il valore {0} non è {1}", (v.isNone() ? std::string("none") : kindName(v)), type));
                break;
            }
            case Op::count: failure = tr("Istruzione non valida"); break;
            }
        } catch (const ScriptError& e) {
            failure = e.what();
        } catch (const std::exception& e) {
            failure = std::string(tr("Errore durante l’esecuzione: ")) + e.what();
        } catch (...) {
            failure = tr("Errore nativo durante l’esecuzione dello script");
        }
        if (!failure.empty()) break;
    }
    profile[fiber.label] += std::chrono::duration<double>(Clock::now() - started).count();
    stats.instructions += executed;
    if (!failure.empty()) {
        fail(fiberPointer, failure);
        return Outcome::failed;
    }
    Value result = fiber.stack.empty() ? Value() : fiber.stack.back();
    finish(fiberPointer, result);
    return Outcome::finished;
}

}
