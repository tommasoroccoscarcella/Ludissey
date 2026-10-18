#include "Types.hpp"

#include "Localization/Localization.hpp"

namespace lim {

using ld::tr;

std::string_view contextName(ExecutionContext context) {
    switch (context) {
    case ExecutionContext::shared: return "condiviso";
    case ExecutionContext::server: return "server";
    case ExecutionContext::client: return "client";
    case ExecutionContext::edit: return "Studio";
    }
    return "condiviso";
}

namespace {
TypePtr make(Type::Kind kind, std::string name = {}) {
    auto t = std::make_shared<Type>();
    t->kind = kind;
    t->name = std::move(name);
    return t;
}
}

TypePtr Type::any() { static TypePtr t = make(Kind::any, "Any"); return t; }
TypePtr Type::none() { static TypePtr t = make(Kind::none, "None"); return t; }
TypePtr Type::boolean() { static TypePtr t = make(Kind::boolean, "Bool"); return t; }
TypePtr Type::integer() { static TypePtr t = make(Kind::integer, "Integer"); return t; }
TypePtr Type::number() { static TypePtr t = make(Kind::number, "Number"); return t; }
TypePtr Type::text() { static TypePtr t = make(Kind::text, "Text"); return t; }
TypePtr Type::error() { static TypePtr t = make(Kind::error, "?"); return t; }

TypePtr Type::list(TypePtr element) {
    auto t = std::make_shared<Type>();
    t->kind = Kind::list;
    t->arguments = {element ? element : any()};
    return t;
}

TypePtr Type::map(TypePtr key, TypePtr value) {
    auto t = std::make_shared<Type>();
    t->kind = Kind::map;
    t->arguments = {key ? key : any(), value ? value : any()};
    return t;
}

TypePtr Type::optional(TypePtr inner) {
    if (!inner || inner->kind == Kind::optional || inner->kind == Kind::any || inner->kind == Kind::none || inner->kind == Kind::error) return inner ? inner : any();
    auto t = std::make_shared<Type>();
    t->kind = Kind::optional;
    t->arguments = {inner};
    return t;
}

TypePtr Type::function(std::vector<TypePtr> parameters, TypePtr result, bool async, int minimum, std::vector<std::string> names) {
    auto t = std::make_shared<Type>();
    t->kind = Kind::function;
    t->arguments = std::move(parameters);
    t->result = result ? result : any();
    t->async = async;
    t->minimumArguments = minimum < 0 ? static_cast<int>(t->arguments.size()) : minimum;
    t->parameterNames = std::move(names);
    return t;
}

TypePtr Type::user(std::string name) { return make(Kind::user, std::move(name)); }
TypePtr Type::enumeration(std::string name) { return make(Kind::enumeration, std::move(name)); }
TypePtr Type::host(std::string name) { return make(Kind::host, std::move(name)); }
TypePtr Type::module(std::string name) { return make(Kind::module, std::move(name)); }

TypePtr Type::task(TypePtr result) {
    auto t = std::make_shared<Type>();
    t->kind = Kind::task;
    t->name = "Task";
    t->result = result ? result : any();
    return t;
}

std::string Type::describe() const {
    switch (kind) {
    case Kind::any: return "Any";
    case Kind::none: return "None";
    case Kind::boolean: return "Bool";
    case Kind::integer: return "Integer";
    case Kind::number: return "Number";
    case Kind::text: return "Text";
    case Kind::error: return "?";
    case Kind::list: return "[" + arguments[0]->describe() + "]";
    case Kind::map: return "[" + arguments[0]->describe() + ": " + arguments[1]->describe() + "]";
    case Kind::optional: return arguments[0]->describe() + "?";
    case Kind::function: {
        std::string out = async ? "async fn(" : "fn(";
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (i) out += ", ";
            if (i < parameterNames.size() && !parameterNames[i].empty()) out += parameterNames[i] + ": ";
            out += arguments[i]->describe();
        }
        out += ")";
        if (result && result->kind != Kind::none) out += " -> " + result->describe();
        return out;
    }
    case Kind::task: return "Task<" + result->describe() + ">";
    case Kind::module: return "modulo " + name;
    default: return name;
    }
}

TypePtr unwrapOptional(const TypePtr& type) { return type && type->kind == Type::Kind::optional ? type->arguments[0] : type; }

bool sameType(const TypePtr& a, const TypePtr& b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return false;
    if (a->name != b->name) return false;
    if (a->arguments.size() != b->arguments.size()) return false;
    for (std::size_t i = 0; i < a->arguments.size(); ++i)
        if (!sameType(a->arguments[i], b->arguments[i])) return false;
    return true;
}

void TypeRegistry::addClass(HostClass definition) { classTable[definition.name] = std::move(definition); }
void TypeRegistry::addGlobal(HostGlobal global) { globalTable[global.name] = std::move(global); }
void TypeRegistry::addFunction(HostMethod function) { functionTable[function.name] = std::move(function); }
void TypeRegistry::addEnum(std::string name, std::vector<std::string> items) { enumTable[std::move(name)] = std::move(items); }

const HostClass* TypeRegistry::findClass(std::string_view name) const {
    auto it = classTable.find(std::string(name));
    return it == classTable.end() ? nullptr : &it->second;
}

const HostGlobal* TypeRegistry::findGlobal(std::string_view name) const {
    auto it = globalTable.find(std::string(name));
    return it == globalTable.end() ? nullptr : &it->second;
}

const HostMethod* TypeRegistry::findFunction(std::string_view name) const {
    auto it = functionTable.find(std::string(name));
    return it == functionTable.end() ? nullptr : &it->second;
}

const std::vector<std::string>* TypeRegistry::findEnum(std::string_view name) const {
    auto it = enumTable.find(std::string(name));
    return it == enumTable.end() ? nullptr : &it->second;
}

bool TypeRegistry::isA(std::string_view name, std::string_view ancestor) const {
    std::string current(name);
    for (int steps = 0; steps < 64 && !current.empty(); ++steps) {
        if (current == ancestor) return true;
        const HostClass* c = findClass(current);
        if (!c) return false;
        current = c->base;
    }
    return false;
}

MemberKind TypeRegistry::subclassMember(std::string_view className, std::string_view name) const {
    std::string key(name);
    for (const auto& [candidate, c] : classTable) {
        if (candidate == className || !isA(candidate, className)) continue;
        if (c.properties.count(key)) return MemberKind::property;
        if (c.methods.count(key)) return MemberKind::method;
        if (c.events.count(key)) return MemberKind::event;
    }
    return MemberKind::none;
}

const HostMember* TypeRegistry::property(std::string_view className, std::string_view name) const {
    std::string current(className);
    for (int steps = 0; steps < 64 && !current.empty(); ++steps) {
        const HostClass* c = findClass(current);
        if (!c) return nullptr;
        if (auto it = c->properties.find(std::string(name)); it != c->properties.end()) return &it->second;
        current = c->base;
    }
    return nullptr;
}

const HostMethod* TypeRegistry::method(std::string_view className, std::string_view name) const {
    std::string current(className);
    for (int steps = 0; steps < 64 && !current.empty(); ++steps) {
        const HostClass* c = findClass(current);
        if (!c) return nullptr;
        if (auto it = c->methods.find(std::string(name)); it != c->methods.end()) return &it->second;
        current = c->base;
    }
    return nullptr;
}

const HostEvent* TypeRegistry::event(std::string_view className, std::string_view name) const {
    std::string current(className);
    for (int steps = 0; steps < 64 && !current.empty(); ++steps) {
        const HostClass* c = findClass(current);
        if (!c) return nullptr;
        if (auto it = c->events.find(std::string(name)); it != c->events.end()) return &it->second;
        current = c->base;
    }
    return nullptr;
}

std::vector<const HostMember*> TypeRegistry::properties(std::string_view className) const {
    std::vector<const HostMember*> out;
    std::map<std::string, bool> seen;
    std::string current(className);
    for (int steps = 0; steps < 64 && !current.empty(); ++steps) {
        const HostClass* c = findClass(current);
        if (!c) break;
        for (const auto& [name, p] : c->properties)
            if (!seen[name]) { seen[name] = true; out.push_back(&p); }
        current = c->base;
    }
    return out;
}

std::vector<const HostMethod*> TypeRegistry::methods(std::string_view className) const {
    std::vector<const HostMethod*> out;
    std::map<std::string, bool> seen;
    std::string current(className);
    for (int steps = 0; steps < 64 && !current.empty(); ++steps) {
        const HostClass* c = findClass(current);
        if (!c) break;
        for (const auto& [name, m] : c->methods)
            if (!seen[name]) { seen[name] = true; out.push_back(&m); }
        current = c->base;
    }
    return out;
}

std::vector<const HostEvent*> TypeRegistry::events(std::string_view className) const {
    std::vector<const HostEvent*> out;
    std::map<std::string, bool> seen;
    std::string current(className);
    for (int steps = 0; steps < 64 && !current.empty(); ++steps) {
        const HostClass* c = findClass(current);
        if (!c) break;
        for (const auto& [name, e] : c->events)
            if (!seen[name]) { seen[name] = true; out.push_back(&e); }
        current = c->base;
    }
    return out;
}

namespace {

HostMethod fn(std::string name, std::vector<std::pair<std::string, TypePtr>> parameters, TypePtr result, int minimum = -1, std::string doc = {}, bool async = false) {
    HostMethod m;
    m.name = std::move(name);
    for (auto& [n, t] : parameters) {
        m.parameterNames.push_back(n);
        m.parameters.push_back(t);
    }
    m.minimum = minimum < 0 ? static_cast<int>(m.parameters.size()) : minimum;
    m.result = result ? result : Type::none();
    m.doc = std::move(doc);
    m.async = async;
    return m;
}

HostMember member(std::string name, TypePtr type, bool readOnly, std::string doc) { return {std::move(name), std::move(type), readOnly, std::move(doc), ExecutionContext::shared, false, ""}; }

}

void addStandardLibrary(TypeRegistry& r) {
    auto N = Type::number(), I = Type::integer(), T = Type::text(), B = Type::boolean(), A = Type::any();
    auto printFn = fn("print", {{"valori", A}}, Type::none(), 0, tr("Scrive i valori nell’Output."));
    printFn.variadic = true;
    r.addFunction(printFn);
    auto warnFn = fn("warn", {{"valori", A}}, Type::none(), 0, tr("Scrive un avviso nell’Output."));
    warnFn.variadic = true;
    r.addFunction(warnFn);
    r.addFunction(fn("error", {{"messaggio", T}}, Type::none(), 1, tr("Interrompe lo script con un errore.")));
    r.addFunction(fn("typeOf", {{"valore", A}}, T, 1, tr("Restituisce il nome del tipo di un valore.")));
    r.addFunction(fn("Text", {{"valore", A}}, T, 1, tr("Converte un valore in testo.")));
    r.addFunction(fn("Number", {{"valore", A}}, Type::optional(N), 1, tr("Converte un testo o un intero in numero.")));
    r.addFunction(fn("Integer", {{"valore", A}}, Type::optional(I), 1, tr("Converte un valore in intero, troncando i decimali.")));
    auto vec3 = Type::host("Vec3"), vec2 = Type::host("Vec2"), color = Type::host("Color"), rotation = Type::host("Rotation");

    HostClass math;
    math.name = "Math";
    math.doc = "Funzioni matematiche.";
    for (auto name : {"abs", "floor", "ceil", "round", "sqrt", "sin", "cos", "tan", "asin", "acos", "atan", "exp", "log", "sign"}) math.statics[name] = fn(name, {{"x", N}}, N);
    math.statics["atan2"] = fn("atan2", {{"y", N}, {"x", N}}, N);
    math.statics["pow"] = fn("pow", {{"base", N}, {"esponente", N}}, N);
    math.statics["min"] = fn("min", {{"a", N}, {"b", N}}, N);
    math.statics["max"] = fn("max", {{"a", N}, {"b", N}}, N);
    math.statics["clamp"] = fn("clamp", {{"x", N}, {"minimo", N}, {"massimo", N}}, N);
    math.statics["lerp"] = fn("lerp", {{"a", N}, {"b", N}, {"t", N}}, N);
    math.constants["pi"] = member("pi", N, true, "π");
    math.constants["huge"] = member("huge", N, true, "Infinito.");
    r.addClass(math);
    r.addGlobal({"Math", Type::host("Math"), "Funzioni matematiche.", ExecutionContext::shared});

    HostClass random;
    random.name = "Random";
    random.creatable = true;
    random.doc = tr("Generatore di numeri casuali.");
    random.constructor = fn("Random", {{"seme", I}}, Type::host("Random"), 0);
    random.methods["next"] = fn("next", {}, N, 0, tr("Numero tra 0 e 1."));
    random.methods["integer"] = fn("integer", {{"minimo", I}, {"massimo", I}}, I, 2, tr("Intero tra minimo e massimo inclusi."));
    random.methods["number"] = fn("number", {{"minimo", N}, {"massimo", N}}, N, 2, tr("Numero tra minimo e massimo."));
    r.addClass(random);
    r.addGlobal({"Random", Type::host("Random"), tr("Generatore di numeri casuali."), ExecutionContext::shared});

    HostClass time;
    time.name = "Time";
    time.doc = tr("Tempo della simulazione e dell’orologio.");
    time.statics["now"] = fn("now", {}, N, 0, tr("Secondi dall’epoca Unix."));
    time.statics["clock"] = fn("clock", {}, N, 0, tr("Secondi dall’avvio dell’esperienza."));
    r.addClass(time);
    r.addGlobal({"Time", Type::host("Time"), "Tempo.", ExecutionContext::shared});

    HostClass task;
    task.name = "TaskLibrary";
    task.doc = tr("Attese e attività in parallelo.");
    task.statics["wait"] = fn("wait", {{"secondi", N}}, N, 0, tr("Attende i secondi indicati."), true);
    task.statics["nextFrame"] = fn("nextFrame", {}, N, 0, tr("Attende il frame successivo."), true);
    task.statics["spawn"] = fn("spawn", {{"funzione", Type::function({}, A)}}, Type::task(A), 1, tr("Avvia una funzione in parallelo."));
    task.statics["delay"] = fn("delay", {{"secondi", N}, {"funzione", Type::function({}, A)}}, Type::task(A), 2, tr("Avvia una funzione dopo un’attesa."));
    task.statics["cancel"] = fn("cancel", {{"attività", Type::task(A)}}, Type::none(), 1, tr("Annulla un’attività."));
    r.addClass(task);
    r.addGlobal({"Task", Type::host("TaskLibrary"), tr("Attese e attività in parallelo."), ExecutionContext::shared});

    HostClass json;
    json.name = "JSON";
    json.doc = "Conversione da e verso JSON.";
    json.statics["encode"] = fn("encode", {{"valore", A}}, T, 1);
    json.statics["decode"] = fn("decode", {{"testo", T}}, A, 1);
    r.addClass(json);
    r.addGlobal({"JSON", Type::host("JSON"), "Conversione JSON.", ExecutionContext::shared});

    HostClass v3;
    v3.name = "Vec3";
    v3.value = true;
    v3.creatable = true;
    v3.doc = "Vettore tridimensionale.";
    v3.constructor = fn("Vec3", {{"x", N}, {"y", N}, {"z", N}}, vec3, 0);
    for (auto n : {"x", "y", "z", "length"}) v3.properties[n] = member(n, N, true, "");
    v3.properties["unit"] = member("unit", vec3, true, tr("Vettore di lunghezza 1."));
    v3.methods["dot"] = fn("dot", {{"altro", vec3}}, N);
    v3.methods["cross"] = fn("cross", {{"altro", vec3}}, vec3);
    v3.methods["lerp"] = fn("lerp", {{"altro", vec3}, {"t", N}}, vec3);
    v3.methods["distance"] = fn("distance", {{"altro", vec3}}, N);
    v3.constants["zero"] = member("zero", vec3, true, "");
    v3.constants["one"] = member("one", vec3, true, "");
    v3.constants["up"] = member("up", vec3, true, "");
    r.addClass(v3);
    HostClass v2;
    v2.name = "Vec2";
    v2.value = true;
    v2.creatable = true;
    v2.doc = "Vettore bidimensionale.";
    v2.constructor = fn("Vec2", {{"x", N}, {"y", N}}, vec2, 0);
    for (auto n : {"x", "y", "length"}) v2.properties[n] = member(n, N, true, "");
    v2.constants["zero"] = member("zero", vec2, true, "");
    r.addClass(v2);
    HostClass c;
    c.name = "Color";
    c.value = true;
    c.creatable = true;
    c.doc = tr("Colore RGB con componenti tra 0 e 1.");
    c.constructor = fn("Color", {{"r", N}, {"g", N}, {"b", N}}, color, 0);
    for (auto n : {"r", "g", "b"}) c.properties[n] = member(n, N, true, "");
    c.properties["hex"] = member("hex", T, true, "Colore esadecimale RRGGBB.");
    c.statics["rgb"] = fn("rgb", {{"r", N}, {"g", N}, {"b", N}}, color, 3, tr("Colore da componenti tra 0 e 1."));
    c.statics["bytes"] = fn("bytes", {{"r", N}, {"g", N}, {"b", N}}, color, 3, tr("Colore da componenti tra 0 e 255."));
    c.statics["hex"] = fn("hex", {{"testo", T}}, color, 1, tr("Colore da testo esadecimale."));
    c.methods["lerp"] = fn("lerp", {{"altro", color}, {"t", N}}, color);
    for (auto n : {"white", "black", "red", "green", "blue", "yellow"}) c.constants[n] = member(n, color, true, "");
    r.addClass(c);
    HostClass rot;
    rot.name = "Rotation";
    rot.value = true;
    rot.creatable = true;
    rot.doc = "Rotazione in gradi attorno agli assi X, Y e Z.";
    rot.constructor = fn("Rotation", {{"x", N}, {"y", N}, {"z", N}}, rotation, 0);
    for (auto n : {"x", "y", "z"}) rot.properties[n] = member(n, N, true, "");
    r.addClass(rot);
    HostClass transform;
    transform.name = "Transform";
    transform.value = true;
    transform.creatable = true;
    transform.doc = "Posizione e rotazione.";
    transform.constructor = fn("Transform", {{"position", vec3}, {"rotation", rotation}}, Type::host("Transform"), 1);
    transform.properties["position"] = member("position", vec3, true, "");
    transform.properties["rotation"] = member("rotation", rotation, true, "");
    transform.properties["lookVector"] = member("lookVector", vec3, true, "Direzione in avanti.");
    transform.properties["rightVector"] = member("rightVector", vec3, true, "");
    transform.properties["upVector"] = member("upVector", vec3, true, "");
    transform.methods["apply"] = fn("apply", {{"punto", vec3}}, vec3, 1, tr("Trasforma un punto locale in coordinate del mondo."));
    r.addClass(transform);
    HostClass ray;
    ray.name = "Ray";
    ray.value = true;
    ray.creatable = true;
    ray.constructor = fn("Ray", {{"origin", vec3}, {"direction", vec3}}, Type::host("Ray"), 2);
    ray.properties["origin"] = member("origin", vec3, true, "");
    ray.properties["direction"] = member("direction", vec3, true, "");
    r.addClass(ray);
    HostClass bounds;
    bounds.name = "Bounds";
    bounds.value = true;
    bounds.creatable = true;
    bounds.constructor = fn("Bounds", {{"low", vec3}, {"high", vec3}}, Type::host("Bounds"), 2);
    bounds.properties["low"] = member("low", vec3, true, "");
    bounds.properties["high"] = member("high", vec3, true, "");
    bounds.properties["center"] = member("center", vec3, true, "");
    bounds.properties["size"] = member("size", vec3, true, "");
    bounds.methods["contains"] = fn("contains", {{"punto", vec3}}, B, 1);
    r.addClass(bounds);
    HostClass dim;
    dim.name = "Dim";
    dim.value = true;
    dim.creatable = true;
    dim.doc = tr("Dimensione di interfaccia: frazione del contenitore più pixel.");
    dim.constructor = fn("Dim", {{"scale", N}, {"offset", N}}, Type::host("Dim"), 0);
    dim.properties["scale"] = member("scale", N, true, "");
    dim.properties["offset"] = member("offset", N, true, "");
    r.addClass(dim);
    HostClass dim2;
    dim2.name = "Dim2";
    dim2.value = true;
    dim2.creatable = true;
    dim2.doc = tr("Posizione o dimensione di interfaccia in due assi.");
    dim2.constructor = fn("Dim2", {{"xScale", N}, {"xOffset", N}, {"yScale", N}, {"yOffset", N}}, Type::host("Dim2"), 0);
    for (auto n : {"xScale", "xOffset", "yScale", "yOffset"}) dim2.properties[n] = member(n, N, true, "");
    dim2.statics["pixels"] = fn("pixels", {{"x", N}, {"y", N}}, Type::host("Dim2"), 2, tr("Solo pixel."));
    dim2.statics["scale"] = fn("scale", {{"x", N}, {"y", N}}, Type::host("Dim2"), 2, tr("Solo frazioni del contenitore."));
    r.addClass(dim2);
}

TypeRegistry standardRegistry() {
    TypeRegistry registry;
    addStandardLibrary(registry);
    return registry;
}

}
