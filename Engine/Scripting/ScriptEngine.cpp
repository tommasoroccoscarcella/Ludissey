#include "ScriptEngine.hpp"

#include "Engine/Character/Character.hpp"
#include "Engine/Core/Json.hpp"
#include "Engine/Core/Rotation.hpp"
#include "Engine/Core/Text.hpp"
#include "Lim/Compiler/Compiler.hpp"
#include "Localization/Localization.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>

namespace ld {

using lim::NativeCall;
using lim::NativeResult;
using lim::ScriptError;
using lim::Value;
using lim::ValueKind;

namespace {

constexpr std::uint64_t cameraHandle = 1, terrainHandle = 2, physicsHandle = 3, networkHandle = 4, tagsHandle = 5, playerDataHandle = 6, firstHandle = 64;
constexpr std::size_t hitMemory = 4096;

double wall() { return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count(); }

std::string upperFirst(std::string text) {
    if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    return text;
}

}

std::string OutputLine::stamped() const {
    std::time_t seconds = static_cast<std::time_t>(time);
    std::tm local{};
    localtime_r(&seconds, &local);
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min, local.tm_sec, static_cast<int>((time - std::floor(time)) * 1000));
    return std::string(buffer) + " - " + text;
}

struct Connection {
    std::uint64_t handle = 0;
    ScriptContext context = ScriptContext::server;
    std::function<void(std::vector<Value>)> handler;
    bool connected = true;
    bool once = false;
};

struct Signal {
    std::weak_ptr<LiveInstance> owner;
    std::string name;
    std::vector<std::shared_ptr<Connection>> connections;
};

struct ContextVm;

class EngineHost final : public lim::Host {
public:
    EngineHost(ScriptEngine::Implementation& owner, ContextVm& context) : implementation(owner), slot(context) {}
    std::optional<Value> global(lim::Vm& vm, std::string_view name) override;
    Value getMember(lim::Vm& vm, const Value& object, const std::string& name) override;
    void setMember(lim::Vm& vm, const Value& object, const std::string& name, const Value& value) override;
    Value connect(lim::Vm& vm, const Value& event, std::function<void(std::vector<Value>)> handler, bool once) override;
    std::string describe(const Value& object) override;
    bool isA(const Value& object, std::string_view type) override;
    std::optional<Value> importModule(lim::Vm& vm, const std::string& name) override;

private:
    ScriptEngine::Implementation& implementation;
    ContextVm& slot;
};

struct ContextVm {
    ScriptContext context = ScriptContext::server;
    std::unique_ptr<lim::Vm> vm;
    std::unique_ptr<EngineHost> host;
    std::map<std::string, Value> modules;
    std::set<std::string> loading;
    std::map<std::string, Value> callbacks;
    std::map<std::string, Value> enums;
    std::shared_ptr<lim::Environment> console;
};

struct ScriptEngine::Implementation {
    explicit Implementation(ScriptEngine& owner) : engine(owner) {}
    ScriptEngine& engine;
    std::map<ScriptContext, std::unique_ptr<ContextVm>> vms;
    std::map<std::string, std::shared_ptr<Signal>> signals;
    std::map<std::uint64_t, InstancePtr> instances;
    std::map<const LiveInstance*, std::uint64_t> instanceHandles;
    std::map<std::uint64_t, std::string> events;
    std::map<std::string, std::uint64_t> eventHandles;
    std::map<std::uint64_t, std::shared_ptr<Connection>> connections;
    std::map<std::uint64_t, ScriptEngine::Hit> hits;
    std::map<std::string, std::shared_ptr<ScriptTween>> tweenPlans;
    std::map<std::string, std::pair<double, int>> remoteBudget;
    std::map<std::string, lim::ModuleInterface> interfaces;
    std::uint64_t nextHandle = firstHandle;

    const EngineTypes& types() const { return EngineTypes::shared(); }

    ContextVm& slot(ScriptContext context) {
        auto& entry = vms[context];
        if (entry) return *entry;
        entry = std::make_unique<ContextVm>();
        entry->context = context;
        entry->vm = std::make_unique<lim::Vm>();
        entry->host = std::make_unique<EngineHost>(*this, *entry);
        entry->vm->setHost(entry->host.get());
        lim::installLibrary(*entry->vm);
        ScriptEngine* owner = &engine;
        entry->vm->setOutput([owner](const lim::OutputMessage& m) {
            OutputLine line;
            line.text = m.text;
            line.kind = m.severity == lim::Severity::error ? OutputLine::Kind::error : m.severity == lim::Severity::warning ? OutputLine::Kind::warning : OutputLine::Kind::info;
            if (!m.source.empty()) line.source = m.source;
            if (m.line) line.line = m.line;
            owner->emit(std::move(line));
        });
        return *entry;
    }

    Value instanceValue(lim::Vm& vm, const InstancePtr& node) {
        if (!node) return Value();
        auto known = instanceHandles.find(node.get());
        std::uint64_t handle;
        if (known != instanceHandles.end()) handle = known->second;
        else {
            handle = nextHandle++;
            instances[handle] = node;
            instanceHandles[node.get()] = handle;
        }
        return vm.hostObject(handle, node->className);
    }

    InstancePtr instanceOf(const Value& value) const {
        if (value.kind != ValueKind::host) return nullptr;
        const auto* h = value.as<lim::HostObject>();
        if (!h->tag.empty()) return nullptr;
        auto it = instances.find(h->handle);
        return it == instances.end() ? nullptr : it->second;
    }

    InstancePtr requireInstance(const Value& value, const std::string& what) const {
        auto node = instanceOf(value);
        if (!node) throw ScriptError(tr("{0}: serve un’istanza, trovato {1}", what, lim::kindName(value)));
        return node;
    }

    std::shared_ptr<Signal> signal(const InstancePtr& node, const std::string& name) {
        std::string key = node->id + "#" + name;
        auto& s = signals[key];
        if (!s) {
            s = std::make_shared<Signal>();
            s->owner = node;
            s->name = name;
        }
        return s;
    }

    Value eventValue(lim::Vm& vm, const InstancePtr& node, const std::string& name) {
        signal(node, name);
        std::string key = node->id + "#" + name;
        auto known = eventHandles.find(key);
        std::uint64_t handle;
        if (known != eventHandles.end()) handle = known->second;
        else {
            handle = nextHandle++;
            events[handle] = key;
            eventHandles[key] = handle;
        }
        return vm.hostObject(handle, "Event", "event");
    }

    void disconnect(const std::shared_ptr<Connection>& connection) {
        if (!connection->connected) return;
        connection->connected = false;
        connection->handler = nullptr;
    }

    Value toValue(lim::Vm& vm, const Variant& v) {
        using K = Variant::Kind;
        switch (v.kind) {
        case K::none: return Value();
        case K::boolean: return Value::boolean(v.flag);
        case K::number: return Value::real(v.numbers[0]);
        case K::text: return vm.text(v.text);
        case K::vector3: return lim::makeVec3(v.numbers[0], v.numbers[1], v.numbers[2]);
        case K::vector2: return lim::makeVec2(v.numbers[0], v.numbers[1]);
        case K::color3: return lim::makeColor(v.numbers[0], v.numbers[1], v.numbers[2]);
        case K::brickColor: {
            Variant c = Variant::color(v.text);
            return lim::makeColor(c.numbers[0], c.numbers[1], c.numbers[2]);
        }
        case K::udim: return lim::makeData(lim::DataKind::dim, {v.numbers[0], v.numbers[1]});
        case K::udim2: return lim::makeData(lim::DataKind::dim2, {v.numbers[0], v.numbers[1], v.numbers[2], v.numbers[3]});
        case K::cframe: {
            Mat3 m;
            m.m = {{{v.numbers[3], v.numbers[4], v.numbers[5]}, {v.numbers[6], v.numbers[7], v.numbers[8]}, {v.numbers[9], v.numbers[10], v.numbers[11]}}};
            Vec3 angles = rotation::euler(m);
            return lim::makeData(lim::DataKind::transform, {v.numbers[0], v.numbers[1], v.numbers[2], angles.x, angles.y, angles.z});
        }
        case K::enumItem: return lim::makeEnum(v.type, v.text);
        case K::instance: return instanceValue(vm, v.instance);
        case K::list: {
            std::vector<Value> items;
            for (const auto& item : *v.items) items.push_back(toValue(vm, item));
            return vm.list(std::move(items));
        }
        case K::map: {
            Value map = vm.map();
            for (const auto& [key, value] : *v.entries) map.as<lim::MapObject>()->set(toValue(vm, key), toValue(vm, value));
            return map;
        }
        }
        return Value();
    }

    Variant fromValue(const Value& value, int depth = 0) const {
        if (depth > 16) throw ScriptError(tr("Valore troppo annidato"));
        switch (value.kind) {
        case ValueKind::none: return Variant::none();
        case ValueKind::boolean: return Variant::boolean(value.flag);
        case ValueKind::integer: return Variant::number(static_cast<double>(value.integer));
        case ValueKind::number: return Variant::number(value.number);
        case ValueKind::text: return Variant::string(value.as<lim::TextObject>()->text);
        case ValueKind::enumValue: {
            const auto* e = value.as<lim::EnumValueObject>();
            return Variant::enumItem(e->type, e->name);
        }
        case ValueKind::data: {
            const auto* d = value.as<lim::DataObject>();
            const auto& n = d->values;
            switch (d->kind) {
            case lim::DataKind::vec3: return Variant::vector3(n[0], n[1], n[2]);
            case lim::DataKind::vec2: return Variant::vector2(n[0], n[1]);
            case lim::DataKind::color: return Variant::color3(n[0], n[1], n[2]);
            case lim::DataKind::dim: return Variant::udim(n[0], n[1]);
            case lim::DataKind::dim2: return Variant::udim2(n[0], n[1], n[2], n[3]);
            case lim::DataKind::rotation: return Variant::vector3(n[0], n[1], n[2]);
            case lim::DataKind::transform: {
                Mat3 m = rotation::matrix(n[3], n[4], n[5]);
                return Variant::cframe({n[0], n[1], n[2], m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2]});
            }
            default: break;
            }
            throw ScriptError(tr("{0} non si può usare qui", std::string(lim::dataName(d->kind))));
        }
        case ValueKind::host: {
            if (auto node = instanceOf(value)) return Variant::object(node);
            throw ScriptError(tr("«{0}» non si può usare qui", value.as<lim::HostObject>()->className));
        }
        case ValueKind::list: {
            std::vector<Variant> items;
            for (const auto& item : value.as<lim::ListObject>()->items) items.push_back(fromValue(item, depth + 1));
            return Variant::list(std::move(items));
        }
        case ValueKind::map: {
            std::vector<std::pair<Variant, Variant>> entries;
            for (const auto& [key, item] : value.as<lim::MapObject>()->entries) entries.emplace_back(fromValue(key, depth + 1), fromValue(item, depth + 1));
            return Variant::map(std::move(entries));
        }
        default: break;
        }
        throw ScriptError(tr("Un valore di tipo {0} non si può inviare al motore", lim::kindName(value)));
    }

    std::size_t weight(const Variant& v) const {
        std::size_t total = 16 + v.text.size() + v.type.size();
        if (v.items)
            for (const auto& item : *v.items) total += weight(item);
        if (v.entries)
            for (const auto& [key, value] : *v.entries) total += weight(key) + weight(value);
        return total;
    }

    std::vector<Variant> arguments(const NativeCall& call, std::size_t from) const {
        std::vector<Variant> out;
        for (std::size_t i = from; i < call.count(); ++i) out.push_back(fromValue(call.argument(i)));
        return out;
    }

    std::vector<Variant> remoteArguments(const InstancePtr& remote, const NativeCall& call, std::size_t from) {
        std::vector<Variant> values;
        try {
            values = arguments(call, from);
        } catch (const ScriptError& e) {
            throw ScriptError(tr("{0}: {1}. Si possono inviare solo testi, numeri, booleani, vettori, colori, enum, istanze, liste e mappe", remote->name, e.what()));
        }
        std::size_t size = 0;
        for (const auto& v : values) size += weight(v);
        if (size > ScriptEngine::remotePayload) throw ScriptError(tr("{0}: dati troppo grandi da inviare ({1} KB, massimo {2} KB)", remote->name, std::to_string(size / 1024), std::to_string(ScriptEngine::remotePayload / 1024)));
        auto& budget = remoteBudget[remote->id];
        double second = std::floor(engine.clock());
        if (budget.first != second) budget = {second, 0};
        if (++budget.second > ScriptEngine::remoteRate) throw ScriptError(tr("{0}: troppi invii in un secondo (massimo {1})", remote->name, std::to_string(ScriptEngine::remoteRate)));
        return values;
    }

    void fire(const InstancePtr& node, const std::string& name, const std::vector<Variant>& values, std::optional<ScriptContext> context) {
        auto it = signals.find(node->id + "#" + name);
        if (it == signals.end()) return;
        auto snapshot = it->second->connections;
        for (const auto& connection : snapshot) {
            if (!connection->connected || (context && connection->context != *context)) continue;
            auto handler = connection->handler;
            if (connection->once) disconnect(connection);
            ContextVm& target = slot(connection->context);
            std::vector<Value> converted;
            for (const auto& v : values) converted.push_back(toValue(*target.vm, v));
            if (handler) handler(std::move(converted));
        }
        auto& list = it->second->connections;
        list.erase(std::remove_if(list.begin(), list.end(), [](const auto& c) { return !c->connected; }), list.end());
    }

    InstancePtr findModule(ScriptContext context, const std::string& name) const {
        std::vector<std::string> roots = {"ReplicatedStorage", "ReplicatedFirst"};
        if (context != ScriptContext::client) {
            roots.push_back("ServerScriptService");
            roots.push_back("ServerStorage");
        }
        roots.push_back("Workspace");
        roots.push_back("StarterPlayer");
        roots.push_back("StarterGui");
        roots.push_back("StarterPack");
        roots.push_back("Lighting");
        for (const auto& root : roots) {
            auto service = engine.model.service(root);
            if (!service) continue;
            for (const auto& node : service->descendants())
                if (node->className == "ModuleScript" && node->name == name && !node->destroyed) return node;
        }
        return nullptr;
    }

    static std::string source(const InstancePtr& node) {
        const PropertyValue* v = node->property("Source");
        return v && (v->kind == PropertyValue::Kind::string || v->kind == PropertyValue::Kind::content) ? v->text : std::string();
    }

    lim::AnalyzeOptions options(ScriptContext context, const std::string& file, const InstancePtr& script) {
        lim::AnalyzeOptions o;
        o.context = limContext(context);
        o.file = file;
        if (script) o.extraGlobals.push_back({"script", lim::Type::host(script->className)});
        o.resolveModule = [this, context](const std::string& name) -> std::optional<lim::ModuleInterface> { return moduleInterface(context, name); };
        return o;
    }

    std::optional<lim::ModuleInterface> moduleInterface(ScriptContext context, const std::string& name) {
        auto module = findModule(context, name);
        if (!module) return std::nullopt;
        std::string text = source(module);
        std::string key = module->id + ":" + sha256Hex(text);
        if (auto cached = interfaces.find(key); cached != interfaces.end()) return cached->second;
        interfaces[key] = lim::ModuleInterface{name, {}, {}, {}};
        lim::AnalyzeOptions o = options(context, module->path(), module);
        auto result = lim::compileSource(text, name, types().registry(), o);
        result.analysis.interface.name = name;
        result.analysis.interface.exports.erase("script");
        interfaces[key] = result.analysis.interface;
        return result.analysis.interface;
    }

    void report(const std::vector<lim::Diagnostic>& diagnostics, const std::string& label) {
        for (const auto& d : diagnostics) {
            bool retired = d.severity == lim::Severity::warning && d.code == "deprecato";
            if (d.severity != lim::Severity::error && !retired) continue;
            OutputLine line;
            line.text = lim::summary(d);
            if (!d.expected.empty() && !d.actual.empty()) line.text += " (serve " + d.expected + ", trovato " + d.actual + ")";
            line.kind = retired ? OutputLine::Kind::warning : OutputLine::Kind::error;
            line.source = label;
            line.line = d.range.start.line;
            engine.emit(std::move(line));
        }
    }

    static InstancePtr humanoidOf(const InstancePtr& model) {
        if (!model || model->className != "Model") return nullptr;
        for (const auto& c : model->children)
            if (c->className == "Humanoid" && !c->destroyed) return c;
        return nullptr;
    }

    static InstancePtr characterOf(const InstancePtr& node) {
        for (auto current = node ? node->parent() : nullptr; current; current = current->parent())
            if (humanoidOf(current)) return current;
        return nullptr;
    }

    InstancePtr requireHumanoid(const InstancePtr& model, const std::string& what) const {
        auto humanoid = humanoidOf(model);
        if (!humanoid) throw ScriptError(tr("{0}: «{1}» non è un personaggio (manca un Humanoid)", what, model->name));
        return humanoid;
    }

    InstancePtr defaultScreen(const InstancePtr& gui) {
        for (const auto& c : gui->children)
            if (c->className == "ScreenGui" && c->name == "UI" && !c->destroyed) return c;
        auto created = engine.model.create("ScreenGui");
        if (!created) throw ScriptError(created.error().message);
        InstancePtr screen = *created;
        if (auto r = engine.model.set(screen, "Name", Variant::string("UI")); !r) throw ScriptError(r.error().message);
        if (auto r = engine.model.setParent(screen, gui); !r) throw ScriptError(r.error().message);
        return screen;
    }

    static const std::map<std::string, std::string>& characterProperties() {
        static const std::map<std::string, std::string> table = {{"$health", "Health"}, {"$maxHealth", "MaxHealth"}, {"$walkSpeed", "WalkSpeed"}, {"$jumpPower", "JumpPower"}};
        return table;
    }

    Value creatorMember(ContextVm& owner, const InstancePtr& node, const std::string& member);
    Value callMethod(ContextVm& owner, const std::string& method, const InstancePtr& target, NativeCall& call);
    Value instanceMember(ContextVm& owner, const InstancePtr& node, const std::string& name);
    Value apiMember(ContextVm& owner, const lim::HostObject& object, const std::string& name);
};

std::optional<Value> EngineHost::global(lim::Vm& vm, std::string_view name) {
    auto& engine = implementation.engine;
    std::string key(name);
    for (const auto& [global, className] : implementation.types().serviceGlobals(slot.context)) {
        if (global != key) continue;
        if (className == "DataModel") return implementation.instanceValue(vm, engine.model.game);
        return implementation.instanceValue(vm, engine.model.service(className));
    }
    if (key == "UI") {
        if (slot.context == ScriptContext::client && engine.localPlayer)
            for (const auto& c : engine.localPlayer->children)
                if (c->className == "PlayerGui") return implementation.instanceValue(vm, c);
        return implementation.instanceValue(vm, engine.model.service("StarterGui"));
    }
    if (key == "Camera") return vm.hostObject(cameraHandle, "CameraView", "api");
    if (key == "Terrain") return vm.hostObject(terrainHandle, "TerrainView", "api");
    if (key == "Physics") return vm.hostObject(physicsHandle, "PhysicsView", "api");
    if (key == "Network") return vm.hostObject(networkHandle, "NetworkView", "api");
    if (key == "Tags") return vm.hostObject(tagsHandle, "TagsView", "api");
    if (auto e = slot.enums.find(key); e != slot.enums.end()) return e->second;
    const auto& enums = implementation.types().registry().enums();
    if (auto e = enums.find(key); e != enums.end()) {
        auto type = std::make_shared<lim::EnumTypeObject>();
        type->name = key;
        type->cases = e->second;
        Value v = Value::make(ValueKind::enumType, type);
        slot.enums[key] = v;
        return v;
    }
    const ClassDescriptor* descriptor = engine.model.registry.find(key);
    if (descriptor && descriptor->canCreate()) {
        auto* impl = &implementation;
        return vm.native(key, [impl, key](NativeCall& call) {
            auto created = impl->engine.model.create(key);
            if (!created) throw ScriptError(created.error().message);
            InstancePtr node = *created;
            const Value& name = call.parameter(1, "name");
            if (name.kind == ValueKind::text) {
                auto r = impl->engine.model.set(node, "Name", Variant::string(name.as<lim::TextObject>()->text));
                if (!r) throw ScriptError(r.error().message);
            }
            const Value& parent = call.parameter(0, "parent");
            if (!parent.isNone()) {
                auto r = impl->engine.model.setParent(node, impl->requireInstance(parent, key));
                if (!r) throw ScriptError(r.error().message);
            }
            return NativeResult::of(impl->instanceValue(call.vm, node));
        });
    }
    return std::nullopt;
}

Value EngineHost::getMember(lim::Vm& vm, const Value& object, const std::string& name) {
    const auto* h = object.as<lim::HostObject>();
    if (h->tag == "api") return implementation.apiMember(slot, *h, name);
    if (h->tag == "event") {
        auto key = implementation.events.find(h->handle);
        if (key == implementation.events.end()) throw ScriptError(tr("Evento non più disponibile"));
        Value self = object;
        auto* host = this;
        if (name == "connect" || name == "once") {
            bool once = name == "once";
            return vm.boundNative(name, Value(), [host, self, once](NativeCall& call) {
                Value function = call.argument(0);
                if (function.kind != ValueKind::function && function.kind != ValueKind::native && function.kind != ValueKind::boundMethod) throw ScriptError(tr("connect: serve una funzione"));
                lim::Vm* target = &call.vm;
                std::string label = call.fiber ? call.fiber->label : std::string("evento");
                return NativeResult::of(host->connect(call.vm, self, [target, function, label](std::vector<Value> values) { target->dispatch(function, std::move(values), label); }, once));
            });
        }
        if (name == "wait") {
            return vm.boundNative(name, Value(), [self](NativeCall&) {
                NativeResult r;
                r.suspension.kind = lim::Suspension::Kind::event;
                r.suspension.target = self;
                return r;
            }, true);
        }
        throw ScriptError(tr("«{0}» non è un membro di Event", name));
    }
    if (h->tag == "connection") {
        auto it = implementation.connections.find(h->handle);
        std::shared_ptr<Connection> connection = it == implementation.connections.end() ? nullptr : it->second;
        if (name == "connected") return Value::boolean(connection && connection->connected);
        if (name == "disconnect") {
            auto* impl = &implementation;
            return vm.boundNative(name, Value(), [impl, connection](NativeCall&) {
                if (connection) impl->disconnect(connection);
                return NativeResult::of(Value());
            });
        }
        throw ScriptError(tr("«{0}» non è un membro di Connection", name));
    }
    if (h->tag == "hit") {
        auto it = implementation.hits.find(h->handle);
        if (it == implementation.hits.end()) throw ScriptError(tr("Risultato del raggio non più disponibile"));
        if (name == "instance") return implementation.instanceValue(vm, it->second.part);
        if (name == "position") return lim::makeVec3(it->second.point.x, it->second.point.y, it->second.point.z);
        if (name == "distance") return Value::real(it->second.distance);
        throw ScriptError(tr("«{0}» non è un membro di RaycastHit", name));
    }
    auto node = implementation.instanceOf(object);
    if (!node) throw ScriptError(tr("Istanza non valida"));
    return implementation.instanceMember(slot, node, name);
}

Value ScriptEngine::Implementation::instanceMember(ContextVm& owner, const InstancePtr& node, const std::string& name) {
    lim::Vm& vm = *owner.vm;
    auto& model = engine.model;
    if (name == "name") return vm.text(node->name);
    if (name == "className") return vm.text(node->className);
    if (name == "parent") return instanceValue(vm, node->parent());
    if (name == "archivable") return Value::boolean(node->archivable);
    if (auto property = types().property(node->className, name)) {
        if (property->front() == '$') return creatorMember(owner, node, *property);
        if (*property == "Name" || *property == "Parent") return instanceMember(owner, node, lowercase(*property));
        auto value = model.get(node, *property);
        if (!value) throw ScriptError(value.error().message);
        const ClassDescriptor* d = model.registry.find(node->className);
        if (value->kind == Variant::Kind::number && d)
            if (auto p = d->properties.find(*property); p != d->properties.end() && (p->second.typeName == "int" || p->second.typeName == "int64")) return Value::from(static_cast<long long>(value->numbers[0]));
        return toValue(vm, *value);
    }
    if (auto method = types().method(node->className, name)) {
        std::string registryName = *method;
        auto* impl = this;
        ContextVm* slotPointer = &owner;
        InstancePtr target = node;
        bool async = registryName == "WaitForChild";
        return vm.boundNative(name, Value(), [impl, slotPointer, registryName, target](NativeCall& call) {
            if (registryName == "WaitForChild") {
                if (call.argument(0).kind != ValueKind::text) throw ScriptError(tr("waitForChild: serve il nome del figlio"));
                std::string child = call.argument(0).as<lim::TextObject>()->text;
                if (auto found = impl->engine.model.find(target, child)) return NativeResult::of(impl->instanceValue(call.vm, found));
                std::weak_ptr<LiveInstance> weak = target;
                lim::Vm* vm = &call.vm;
                NativeResult r;
                r.suspension.kind = lim::Suspension::Kind::poll;
                r.suspension.poll = [impl, weak, child, vm]() -> std::optional<Value> {
                    auto owner = weak.lock();
                    if (!owner) return std::nullopt;
                    auto found = impl->engine.model.find(owner, child);
                    if (!found) return std::nullopt;
                    return impl->instanceValue(*vm, found);
                };
                if (call.argument(1).numeric()) r.suspension.timeout = call.argument(1).toNumber();
                return r;
            }
            return NativeResult::of(impl->callMethod(*slotPointer, registryName, target, call));
        }, async);
    }
    if (auto event = types().event(node->className, name)) {
        if (*event == "$received") return eventValue(vm, node, owner.context == ScriptContext::client ? "OnClientEvent" : "OnServerEvent");
        return eventValue(vm, node, *event);
    }
    throw ScriptError(tr("«{0}» non è un membro di {1}", name, node->className));
}

Value ScriptEngine::Implementation::creatorMember(ContextVm& owner, const InstancePtr& node, const std::string& member) {
    lim::Vm& vm = *owner.vm;
    if (member == "$all") {
        std::vector<Value> players;
        for (const auto& c : node->children)
            if (c->className == "Player" && !c->destroyed) players.push_back(instanceValue(vm, c));
        return vm.list(std::move(players));
    }
    if (member == "$character") return instanceValue(vm, characterOf(node));
    if (member == "$screen") return instanceValue(vm, defaultScreen(node));
    if (member == "$humanoid") return instanceValue(vm, requireHumanoid(node, "humanoid"));
    if (member == "$player") {
        auto players = engine.model.service("Players");
        if (players)
            for (const auto& c : players->children) {
                const PropertyValue* v = c->property("Character");
                if (c->className == "Player" && v && v->kind == PropertyValue::Kind::reference && v->text == node->id) return instanceValue(vm, c);
            }
        return Value();
    }
    if (auto mapped = characterProperties().find(member); mapped != characterProperties().end()) {
        auto value = engine.model.get(requireHumanoid(node, member.substr(1)), mapped->second);
        if (!value) throw ScriptError(value.error().message);
        return toValue(vm, *value);
    }
    throw ScriptError(tr("«{0}» non è un membro di {1}", member.substr(1), node->className));
}

void EngineHost::setMember(lim::Vm&, const Value& object, const std::string& name, const Value& value) {
    auto node = implementation.instanceOf(object);
    if (!node) throw ScriptError(tr("«{0}» non si può assegnare su {1}", name, describe(object)));
    if (name == "onInvoke" || name == "onServerInvoke" || name == "onClientInvoke") {
        std::string key = node->id + "." + upperFirst(name);
        if (value.isNone()) slot.callbacks.erase(key);
        else if (value.kind == ValueKind::function || value.kind == ValueKind::native || value.kind == ValueKind::boundMethod) slot.callbacks[key] = value;
        else throw ScriptError(tr("{0}: serve una funzione", name));
        return;
    }
    auto& model = implementation.engine.model;
    std::string property;
    if (name == "name") property = "Name";
    else if (name == "parent") property = "Parent";
    else if (name == "archivable") property = "Archivable";
    else if (auto p = implementation.types().property(node->className, name)) property = *p;
    else throw ScriptError(tr("«{0}» non è un membro di {1}", name, node->className));
    if (property.front() == '$') {
        auto mapped = ScriptEngine::Implementation::characterProperties().find(property);
        if (mapped == ScriptEngine::Implementation::characterProperties().end()) throw ScriptError(tr("«{0}» di {1} è di sola lettura", name, node->className));
        if (!value.numeric()) throw ScriptError(tr("{0}: serve un numero", name));
        auto r = model.set(implementation.requireHumanoid(node, name), mapped->second, Variant::number(value.toNumber()));
        if (!r) throw ScriptError(r.error().message);
        return;
    }
    Variant v = implementation.fromValue(value);
    if (v.kind == Variant::Kind::enumItem) {
        const ClassDescriptor* d = model.registry.find(node->className);
        if (d)
            if (auto p = d->properties.find(property); p != d->properties.end() && p->second.category == "Enum" && v.type != p->second.typeName)
                throw ScriptError(tr("{0}: serve {1}, trovato {2}", name, p->second.typeName, v.type));
    }
    auto r = model.set(node, property, v);
    if (!r) throw ScriptError(r.error().message);
    if (node->className == "Sound" && property == "Playing" && implementation.engine.soundPlayer) {
        auto playing = model.get(node, "Playing");
        implementation.engine.soundPlayer(node, playing && playing->kind == Variant::Kind::boolean && playing->flag);
    }
}

Value EngineHost::connect(lim::Vm& vm, const Value& event, std::function<void(std::vector<Value>)> handler, bool once) {
    const auto* h = event.as<lim::HostObject>();
    auto key = implementation.events.find(h->handle);
    if (key == implementation.events.end()) throw ScriptError(tr("Evento non più disponibile"));
    auto separator = key->second.find('#');
    auto node = implementation.engine.model.find(key->second.substr(0, separator));
    std::shared_ptr<Signal> signal;
    if (auto it = implementation.signals.find(key->second); it != implementation.signals.end()) signal = it->second;
    if (!signal) {
        if (!node) throw ScriptError(tr("L’istanza dell’evento è stata distrutta"));
        signal = implementation.signal(node, key->second.substr(separator + 1));
    }
    auto connection = std::make_shared<Connection>();
    connection->handle = implementation.nextHandle++;
    connection->context = slot.context;
    connection->handler = std::move(handler);
    connection->once = once;
    signal->connections.push_back(connection);
    implementation.connections[connection->handle] = connection;
    return vm.hostObject(connection->handle, "Connection", "connection");
}

std::string EngineHost::describe(const Value& object) {
    if (auto node = implementation.instanceOf(object)) return node->name;
    return object.as<lim::HostObject>()->className;
}

bool EngineHost::isA(const Value& object, std::string_view type) {
    if (auto node = implementation.instanceOf(object)) {
        if (type == "Character") return ScriptEngine::Implementation::humanoidOf(node) != nullptr;
        return implementation.engine.model.registry.isA(node->className, type);
    }
    return object.as<lim::HostObject>()->className == type;
}

std::optional<Value> EngineHost::importModule(lim::Vm& vm, const std::string& name) {
    auto module = implementation.findModule(slot.context, name);
    if (!module) return std::nullopt;
    if (auto cached = slot.modules.find(module->id); cached != slot.modules.end()) return cached->second;
    if (slot.loading.count(module->id)) throw ScriptError(tr("Il modulo «{0}» è importato in modo ricorsivo", name));
    slot.loading.insert(module->id);
    struct Release {
        ContextVm& slot;
        std::string id;
        ~Release() { slot.loading.erase(id); }
    } release{slot, module->id};
    std::string label = module->path();
    auto result = lim::compileSource(ScriptEngine::Implementation::source(module), label, implementation.types().registry(), implementation.options(slot.context, label, module));
    if (!result.module) {
        implementation.report(result.diagnostics, label);
        throw ScriptError(tr("Il modulo «{0}» contiene errori", name));
    }
    implementation.report(result.diagnostics, label);
    auto compiled = std::make_shared<const lim::Module>(std::move(*result.module));
    auto environment = vm.newEnvironment(label, {{"script", implementation.instanceValue(vm, module)}});
    std::string error;
    Value exported = vm.runModule(compiled, environment, label, &error);
    if (exported.isNone()) throw ScriptError(tr("Il modulo «{0}» non si è caricato: {1}", name, error));
    exported.as<lim::ModuleObject>()->exports.erase("script");
    exported.as<lim::ModuleObject>()->name = name;
    slot.modules[module->id] = exported;
    return exported;
}

Value ScriptEngine::Implementation::apiMember(ContextVm& owner, const lim::HostObject& object, const std::string& name) {
    lim::Vm& vm = *owner.vm;
    auto* impl = this;
    auto vector = [](const Value& v, const char* what) {
        if (!lim::isData(v, lim::DataKind::vec3)) throw ScriptError(tr("{0}: serve un Vec3", std::string(what)));
        const auto& n = v.as<lim::DataObject>()->values;
        return Vec3{n[0], n[1], n[2]};
    };
    auto text = [](const Value& v, const char* what) {
        if (v.kind != ValueKind::text) throw ScriptError(tr("{0}: serve un testo", std::string(what)));
        return v.as<lim::TextObject>()->text;
    };
    switch (object.handle) {
    case cameraHandle: {
        CameraState state = engine.camera ? engine.camera() : CameraState{};
        if (name == "position") return lim::makeVec3(state.position.x, state.position.y, state.position.z);
        if (name == "focus") return lim::makeVec3(state.focus.x, state.focus.y, state.focus.z);
        if (name == "lookVector") {
            Vec3 d = (state.focus - state.position).normalized();
            return lim::makeVec3(d.x, d.y, d.z);
        }
        if (name == "fieldOfView") return Value::real(state.fieldOfView);
        if (name == "firstPerson") return Value::boolean(state.firstPerson);
        break;
    }
    case terrainHandle: {
        Terrain* terrain = engine.terrain;
        if (name == "fillSphere") {
            if (owner.context == ScriptContext::client) throw ScriptError(tr("Terrain.fillSphere è disponibile solo negli script del server"));
            return vm.native(name, [impl, terrain, vector](NativeCall& c) {
                Vec3 point = vector(c.parameter(0, "center"), "fillSphere");
                const Value& radius = c.parameter(1, "radius");
                if (radius.kind != ValueKind::integer && radius.kind != ValueKind::number) throw ScriptError(tr("fillSphere: serve un raggio numerico"));
                const Value& material = c.parameter(2, "material");
                if (material.kind != ValueKind::enumValue || material.as<lim::EnumValueObject>()->type != "TerrainMaterial")
                    throw ScriptError(tr("fillSphere: serve un TerrainMaterial, per esempio .grass"));
                static const std::map<std::string, TerrainMaterial> materials = {{"air", TerrainMaterial::air}, {"grass", TerrainMaterial::grass}, {"rock", TerrainMaterial::rock},
                                                                                 {"sand", TerrainMaterial::sand}, {"mud", TerrainMaterial::mud}, {"water", TerrainMaterial::water}};
                auto chosen = materials.find(material.as<lim::EnumValueObject>()->name);
                if (chosen == materials.end()) throw ScriptError("fillSphere: materiale sconosciuto");
                if (!terrain) throw ScriptError(tr("fillSphere: terreno non disponibile in questo contesto"));
                auto changed = terrain->fillSphere(point, radius.toNumber(), chosen->second);
                if (!changed) throw ScriptError(changed.error().message);
                if (*changed > 0) impl->engine.model.touch();
                return NativeResult::of(Value::from(static_cast<long long>(*changed)));
            });
        }
        if (name == "height") return vm.native(name, [terrain](NativeCall& c) {
            if (!terrain || terrain->empty()) return NativeResult::of(Value());
            auto h = terrain->surface(c.argument(0).toNumber(), c.argument(1).toNumber(), rules::terrain::bottom + Terrain::cellsY * Terrain::cellSize);
            return NativeResult::of(h ? Value::real(*h) : Value());
        });
        if (name == "isSolid" || name == "isWater") {
            bool water = name == "isWater";
            return vm.native(name, [terrain, water, vector](NativeCall& c) {
                Vec3 p = vector(c.argument(0), water ? "isWater" : "isSolid");
                return NativeResult::of(Value::boolean(terrain && (water ? terrain->water(p.x, p.y, p.z) : terrain->solid(p.x, p.y, p.z))));
            });
        }
        if (name == "raycast") return vm.native(name, [terrain, vector](NativeCall& c) {
            Vec3 o = vector(c.argument(0), "raycast"), d = vector(c.argument(1), "raycast");
            double length = d.length();
            if (!terrain || length <= 0) return NativeResult::of(Value());
            auto hit = terrain->raycast(o, d * (1 / length), length);
            return NativeResult::of(hit ? Value::real(*hit) : Value());
        });
        break;
    }
    case playerDataHandle: {
        if (owner.context == ScriptContext::client) throw ScriptError(tr("Storage.player è disponibile solo negli script del server"));
        if (name != "load" && name != "save") break;
        bool saving = name == "save";
        return vm.native(name, [impl, saving](NativeCall& c) {
            struct Pending {
                bool done = false;
                std::optional<Json> value;
                std::string error;
            };
            auto pending = std::make_shared<Pending>();
            Json payload;
            if (saving) {
                std::string encoded = c.vm.fromValueToJson(c.parameter(0, "value"));
                if (encoded.size() > static_cast<std::size_t>(rules::scriptData::characters)) throw ScriptError(tr("save: dati troppo grandi (massimo {0} caratteri)", std::to_string(rules::scriptData::characters)));
                auto parsed = Json::parse(encoded);
                if (!parsed) throw ScriptError(tr("save: valore non salvabile"));
                payload = *parsed;
            }
            ScriptEngine* engine = &impl->engine;
            if (!engine->storage) {
                OutputLine line;
                line.text = std::string(tr("Storage: servizio dati non disponibile in questa sessione"));
                line.kind = OutputLine::Kind::warning;
                engine->emit(std::move(line));
                return NativeResult::of(saving ? Value::boolean(false) : Value());
            }
            engine->storage(saving ? "save" : "load", payload, [pending](std::optional<Json> value, std::string error) {
                pending->value = std::move(value);
                pending->error = std::move(error);
                pending->done = true;
            });
            lim::Vm* vm = &c.vm;
            NativeResult r;
            r.suspension.kind = lim::Suspension::Kind::poll;
            r.suspension.poll = [pending, engine, vm, saving]() -> std::optional<Value> {
                if (!pending->done) return std::nullopt;
                if (!pending->error.empty()) {
                    OutputLine line;
                    line.text = "Storage: " + pending->error;
                    line.kind = OutputLine::Kind::warning;
                    engine->emit(std::move(line));
                    return saving ? Value::boolean(false) : Value();
                }
                if (saving) return Value::boolean(true);
                if (!pending->value || pending->value->isNull()) return Value();
                return vm->toJsonValue(pending->value->dump());
            };
            return r;
        }, true);
    }
    case physicsHandle: {
        if (name == "gravity") return Value::real(motion::gravity);
        if (name == "raycast") return vm.native(name, [impl, vector](NativeCall& c) {
            Vec3 o = vector(c.parameter(0, "origin"), "raycast"), d = vector(c.parameter(1, "direction"), "raycast");
            std::vector<InstancePtr> ignored;
            const Value& ignore = c.parameter(2, "ignore");
            if (ignore.kind == ValueKind::list)
                for (const auto& item : ignore.as<lim::ListObject>()->items)
                    if (auto node = impl->instanceOf(item)) ignored.push_back(node);
            Hit hit = impl->engine.raycast(o, d, ignored);
            if (!hit.part) return NativeResult::of(Value());
            std::uint64_t handle = impl->nextHandle++;
            impl->hits[handle] = hit;
            while (impl->hits.size() > hitMemory) impl->hits.erase(impl->hits.begin());
            return NativeResult::of(c.vm.hostObject(handle, "RaycastHit", "hit"));
        });
        break;
    }
    case networkHandle: {
        if (name == "isServer") return Value::boolean(owner.context == ScriptContext::server);
        if (name == "isClient") return Value::boolean(owner.context == ScriptContext::client);
        if (name == "event" || name == "remoteFunction") {
            std::string className = name == "event" ? "RemoteEvent" : "RemoteFunction";
            bool create = name == "event";
            return vm.native(name, [impl, className, text, create](NativeCall& c) {
                std::string wanted = text(c.argument(0), "Network");
                if (wanted.empty() || wanted.size() > 100) throw ScriptError(tr("Network: il nome del canale deve avere da 1 a 100 caratteri"));
                auto storage = impl->engine.model.service("ReplicatedStorage");
                for (const auto& node : storage->descendants())
                    if (node->className == className && node->name == wanted && !node->destroyed) return NativeResult::of(impl->instanceValue(c.vm, node));
                if (!create) return NativeResult::of(Value());
                auto made = impl->engine.model.create(className);
                if (!made) throw ScriptError(made.error().message);
                if (auto r = impl->engine.model.set(*made, "Name", Variant::string(wanted)); !r) throw ScriptError(r.error().message);
                if (auto r = impl->engine.model.setParent(*made, storage); !r) throw ScriptError(r.error().message);
                return NativeResult::of(impl->instanceValue(c.vm, *made));
            });
        }
        break;
    }
    case tagsHandle: {
        if (name == "add" || name == "remove" || name == "has") {
            std::string action = name;
            return vm.native(name, [impl, action, text](NativeCall& c) {
                auto node = impl->requireInstance(c.argument(0), "Tags." + action);
                std::string tag = text(c.argument(1), "Tags");
                if (action == "add") node->tags.insert(tag);
                else if (action == "remove") node->tags.erase(tag);
                else return NativeResult::of(Value::boolean(node->tags.count(tag) > 0));
                return NativeResult::of(Value());
            });
        }
        if (name == "tagged") return vm.native(name, [impl, text](NativeCall& c) {
            std::string tag = text(c.argument(0), "Tags.tagged");
            std::vector<Value> out;
            for (const auto& node : impl->engine.model.game->descendants())
                if (node->tags.count(tag)) out.push_back(impl->instanceValue(c.vm, node));
            return NativeResult::of(c.vm.list(std::move(out)));
        });
        if (name == "of") return vm.native(name, [impl](NativeCall& c) {
            auto node = impl->requireInstance(c.argument(0), "Tags.of");
            std::vector<Value> out;
            for (const auto& tag : node->tags) out.push_back(c.vm.text(tag));
            return NativeResult::of(c.vm.list(std::move(out)));
        });
        break;
    }
    default: break;
    }
    throw ScriptError(tr("«{0}» non è un membro di {1}", name, object.className));
}

Value ScriptEngine::Implementation::callMethod(ContextVm& owner, const std::string& method, const InstancePtr& target, NativeCall& call) {
    lim::Vm& vm = call.vm;
    auto& model = engine.model;
    auto text = [&](std::size_t i, const std::string& name) -> std::string {
        const Value& v = call.argument(i);
        if (v.kind != ValueKind::text) throw ScriptError(tr("{0}: serve un testo come {1}", method, name));
        return v.as<lim::TextObject>()->text;
    };
    auto list = [&](const std::vector<InstancePtr>& nodes) {
        std::vector<Value> items;
        for (const auto& n : nodes) items.push_back(instanceValue(vm, n));
        return vm.list(std::move(items));
    };
    auto check = [](const Result<void>& r) {
        if (!r) throw ScriptError(r.error().message);
    };
    const auto& registry = model.registry;
    if (method == "$now") return Value::real(wall());
    if (method == "$clock") return Value::real(engine.clock());
    if (method == "$playerData") {
        const Value& who = call.argument(0);
        auto local = engine.localPlayer;
        bool matches = false;
        if (auto node = instanceOf(who)) matches = node == local;
        else if (who.numeric() && local) {
            auto id = model.get(local, "UserId");
            matches = id && id->kind == Variant::Kind::number && id->numbers[0] == who.toNumber();
        }
        if (!local) throw ScriptError(tr("Storage.player: nessun giocatore in questa sessione"));
        if (!matches) throw ScriptError(tr("Storage.player: ogni sessione può leggere e salvare solo i dati del proprio giocatore"));
        return vm.hostObject(playerDataHandle, "PlayerData", "api");
    }
    if (method == "$takeDamage") {
        auto humanoid = requireHumanoid(target, "takeDamage");
        auto health = model.get(humanoid, "Health");
        double current = health && health->kind == Variant::Kind::number ? health->numbers[0] : 100;
        check(model.set(humanoid, "Health", Variant::number(std::max(0.0, current - call.argument(0).toNumber()))));
        return Value();
    }
    if (method == "$add") {
        check(model.setParent(requireInstance(call.parameter(0, "child"), "add"), target));
        return Value();
    }
    if (method == "FindFirstChild") {
        const Value& recursive = call.argument(1);
        return instanceValue(vm, model.find(target, text(0, "nome"), recursive.kind == ValueKind::boolean && recursive.flag));
    }
    if (method == "FindFirstChildOfClass") {
        std::string name = text(0, "classe");
        for (const auto& c : target->children)
            if (c->className == name) return instanceValue(vm, c);
        return Value();
    }
    if (method == "FindFirstChildWhichIsA") {
        std::string name = text(0, "classe");
        for (const auto& c : target->children)
            if (registry.isA(c->className, name)) return instanceValue(vm, c);
        return Value();
    }
    if (method == "FindFirstAncestor" || method == "FindFirstAncestorOfClass" || method == "FindFirstAncestorWhichIsA") {
        std::string name = text(0, "nome");
        for (auto node = target->parent(); node && node->className != "DataModel"; node = node->parent())
            if (node->name == name || node->className == name || registry.isA(node->className, name)) return instanceValue(vm, node);
        return Value();
    }
    if (method == "GetChildren") return list(target->children);
    if (method == "GetDescendants") return list(target->descendants());
    if (method == "IsA") return Value::boolean(registry.isA(target->className, text(0, "classe")));
    if (method == "Clone") return instanceValue(vm, model.clone(target));
    if (method == "Destroy") {
        model.destroy(target);
        return Value();
    }
    if (method == "ClearAllChildren") {
        auto children = target->children;
        for (const auto& c : children) model.destroy(c);
        return Value();
    }
    if (method == "GetFullName") return vm.text(target->path());
    if (method == "IsAncestorOf") {
        auto other = instanceOf(call.argument(0));
        return Value::boolean(other && other->isDescendantOf(*target));
    }
    if (method == "IsDescendantOf") {
        auto other = instanceOf(call.argument(0));
        return Value::boolean(other && target->isDescendantOf(*other));
    }
    if (method == "GetPropertyChangedSignal") {
        std::string property = text(0, tr("proprietà"));
        auto mapped = types().property(target->className, property);
        return eventValue(vm, target, "Changed:" + (mapped ? *mapped : property));
    }
    if (method == "GetService" || method == "FindService") {
        std::string name = text(0, "servizio");
        auto service = model.service(name);
        if (!service && method == "GetService") throw ScriptError(tr("«{0}» non è un servizio valido", name));
        return instanceValue(vm, service);
    }
    if (method == "GetPlayers") {
        std::vector<InstancePtr> players;
        for (const auto& c : target->children)
            if (c->className == "Player") players.push_back(c);
        return list(players);
    }
    if (method == "GetPlayerFromCharacter") {
        auto character = instanceOf(call.argument(0));
        for (const auto& c : target->children) {
            if (c->className != "Player") continue;
            const PropertyValue* ref = c->property("Character");
            auto id = ref ? ref->referenceId() : std::nullopt;
            if (id && character && *id == character->id) return instanceValue(vm, c);
        }
        return Value();
    }
    if (method == "MoveTo") {
        Variant v = fromValue(call.argument(0));
        if (v.kind != Variant::Kind::vector3) throw ScriptError(tr("moveTo: serve un Vec3"));
        engine.translate(target, {v.numbers[0], v.numbers[1], v.numbers[2]});
        return Value();
    }
    if (method == "SetPrimaryPartCFrame" || method == "GetPrimaryPartCFrame") {
        const PropertyValue* ref = target->property("PrimaryPart");
        auto id = ref ? ref->referenceId() : std::nullopt;
        auto primary = id ? model.find(*id) : nullptr;
        if (method == "GetPrimaryPartCFrame") {
            if (!primary || !primary->block) return Value();
            return toValue(vm, Variant::cframe(DataModel::frame(*primary->block)));
        }
        Variant frame = fromValue(call.argument(0));
        if (frame.kind != Variant::Kind::cframe) throw ScriptError(tr("setPrimaryPartTransform: serve un Transform"));
        if (!primary || !primary->block) throw ScriptError(tr("setPrimaryPartTransform: il modello non ha una parte principale (primaryPart)"));
        double dx = frame.numbers[0] - primary->block->x, dy = frame.numbers[1] - primary->block->y, dz = frame.numbers[2] - primary->block->z;
        for (const auto& node : target->descendants()) {
            if (!node->block || node->className == "Model") continue;
            node->block->x += dx;
            node->block->y += dy;
            node->block->z += dz;
        }
        check(model.set(primary, "CFrame", frame));
        model.touch();
        return Value();
    }
    if (method == "TakeDamage") {
        double amount = call.argument(0).toNumber();
        auto health = model.get(target, "Health");
        double current = health && health->kind == Variant::Kind::number ? health->numbers[0] : 100;
        check(model.set(target, "Health", Variant::number(std::max(0.0, current - amount))));
        return Value();
    }
    if (method == "Play" || method == "Resume") {
        if (target->className == "Tween") {
            engine.playTween(target);
            return Value();
        }
        check(model.set(target, "Playing", Variant::boolean(true)));
        if (engine.soundPlayer) engine.soundPlayer(target, true);
        return Value();
    }
    if (method == "Stop" || method == "Pause" || method == "Cancel") {
        if (target->className == "Tween") {
            engine.cancelTween(target);
            return Value();
        }
        check(model.set(target, "Playing", Variant::boolean(false)));
        if (engine.soundPlayer) engine.soundPlayer(target, false);
        return Value();
    }
    if (method == "Fire") {
        fire(target, "Event", arguments(call, 0), owner.context);
        return Value();
    }
    auto invoke = [&](const std::string& callback, ScriptContext context, std::vector<Variant> values) -> Value {
        ContextVm& receiver = slot(context);
        auto function = receiver.callbacks.find(target->id + "." + callback);
        if (function == receiver.callbacks.end()) throw ScriptError(tr("{0} non è impostato su {1}", callback, target->name));
        std::vector<Value> converted;
        for (const auto& v : values) converted.push_back(toValue(*receiver.vm, v));
        Value result = receiver.vm->callSync(function->second, std::move(converted));
        return toValue(vm, fromValue(result));
    };
    auto local = [&] { return Variant::object(engine.localPlayer); };
    if (method == "Invoke") return invoke("OnInvoke", owner.context, arguments(call, 0));
    if (method == "FireServer") {
        auto values = remoteArguments(target, call, 0);
        values.insert(values.begin(), local());
        fire(target, "OnServerEvent", values, ScriptContext::server);
        return Value();
    }
    if (method == "FireClient") {
        requireInstance(call.argument(0), "fireClient");
        fire(target, "OnClientEvent", remoteArguments(target, call, 1), ScriptContext::client);
        return Value();
    }
    if (method == "FireAllClients") {
        fire(target, "OnClientEvent", remoteArguments(target, call, 0), ScriptContext::client);
        return Value();
    }
    if (method == "InvokeServer") {
        auto values = remoteArguments(target, call, 0);
        values.insert(values.begin(), local());
        return invoke("OnServerInvoke", ScriptContext::server, values);
    }
    if (method == "InvokeClient") {
        requireInstance(call.argument(0), "invokeClient");
        return invoke("OnClientInvoke", ScriptContext::client, remoteArguments(target, call, 1));
    }
    if (method == "AddItem") {
        auto item = instanceOf(call.argument(0));
        if (!item) return Value();
        double lifetime = call.argument(1).numeric() ? call.argument(1).toNumber() : 10;
        std::weak_ptr<LiveInstance> weak = item;
        DataModel* m = &model;
        engine.schedule(lifetime, [weak, m] {
            if (auto node = weak.lock()) m->destroy(node);
        });
        return Value();
    }
    if (method == "Get") {
        std::vector<InstancePtr> alive;
        for (const auto& n : engine.selection)
            if (!n->destroyed) alive.push_back(n);
        return list(alive);
    }
    if (method == "Set") {
        const Value& chosen = call.argument(0);
        if (chosen.kind != ValueKind::list) throw ScriptError(tr("set: serve una lista di istanze"));
        std::vector<InstancePtr> nodes;
        for (const auto& item : chosen.as<lim::ListObject>()->items)
            if (auto n = instanceOf(item)) nodes.push_back(n);
        engine.selection = nodes;
        fire(target, "SelectionChanged", {}, std::nullopt);
        return Value();
    }
    if (method == "IsClient") return Value::boolean(owner.context == ScriptContext::client);
    if (method == "IsServer") return Value::boolean(owner.context == ScriptContext::server);
    if (method == "IsStudio") return Value::boolean(engine.studio);
    if (method == "IsRunning") return Value::boolean(true);
    if (method == "IsKeyDown") {
        const Value& key = call.argument(0);
        if (key.kind != ValueKind::enumValue) return Value::boolean(false);
        return Value::boolean(engine.keys.count(key.as<lim::EnumValueObject>()->name) > 0);
    }
    if (method == "GetFocusedTextBox") return instanceValue(vm, engine.focusedTextBox);
    if (method == "CaptureFocus") {
        engine.focusedTextBox = target;
        return Value();
    }
    if (method == "ReleaseFocus") {
        if (engine.focusedTextBox == target) {
            engine.focusedTextBox = nullptr;
            fire(target, "FocusLost", {Variant::boolean(false)}, std::nullopt);
        }
        return Value();
    }
    if (method == "IsFocused") return Value::boolean(engine.focusedTextBox == target);
    auto enumName = [&](const Value& v, std::string fallback) {
        return v.kind == ValueKind::enumValue ? v.as<lim::EnumValueObject>()->name : fallback;
    };
    if (method == "Create") {
        auto subject = requireInstance(call.parameter(0, "instance"), "create");
        const Value& goals = call.parameter(1, "goals");
        if (goals.kind != ValueKind::map) throw ScriptError(tr("create: servono gli obiettivi come mappa, per esempio [\"transparency\": 1]"));
        auto plan = std::make_shared<ScriptTween>();
        plan->target = subject;
        const Value& duration = call.parameter(2, "duration");
        plan->duration = duration.numeric() ? duration.toNumber() : 1;
        plan->style = enumName(call.parameter(3, "style"), "Quad");
        plan->direction = enumName(call.parameter(4, "direction"), "Out");
        for (const auto& [key, goal] : goals.as<lim::MapObject>()->entries) {
            if (key.kind != ValueKind::text) continue;
            std::string property = key.as<lim::TextObject>()->text;
            auto mapped = types().property(subject->className, property);
            if (!mapped) throw ScriptError(tr("create: «{0}» non è una proprietà di {1}", property, subject->className));
            plan->goals[*mapped] = fromValue(goal);
        }
        auto tween = model.make("Tween");
        tweenPlans[tween->id] = plan;
        return instanceValue(vm, tween);
    }
    if (method == "TweenPosition" || method == "TweenSize" || method == "TweenSizeAndPosition") {
        std::vector<std::string> properties = method == "TweenPosition" ? std::vector<std::string>{"Position"} : method == "TweenSize" ? std::vector<std::string>{"Size"} : std::vector<std::string>{"Size", "Position"};
        auto tween = std::make_shared<ScriptTween>();
        tween->target = target;
        for (std::size_t i = 0; i < properties.size(); ++i) tween->goals[properties[i]] = fromValue(call.argument(i));
        std::size_t base = properties.size();
        tween->direction = enumName(call.argument(base), "Out");
        tween->style = enumName(call.argument(base + 1), "Quad");
        tween->duration = call.argument(base + 2).numeric() ? call.argument(base + 2).toNumber() : 1;
        for (const auto& p : properties)
            if (auto v = model.get(target, p)) tween->starts[p] = *v;
        engine.startTween(tween);
        return Value::boolean(true);
    }
    if (method == "JSONEncode") {
        std::string out = vm.fromValueToJson(call.argument(0));
        return vm.text(out);
    }
    if (method == "JSONDecode") return vm.toJsonValue(text(0, "JSON"));
    if (method == "GenerateGUID") {
        std::string raw = uppercase(makeUuid());
        const Value& braces = call.argument(0);
        return vm.text(braces.kind == ValueKind::boolean && !braces.flag ? raw : "{" + raw + "}");
    }
    throw ScriptError(tr("«{0}» non è disponibile in Ludissey", method));
}

double ScriptTween::ease(double t, const std::string& style, const std::string& direction) {
    auto curve = [&](double x) {
        if (style == "Quad") return x * x;
        if (style == "Quart") return x * x * x * x;
        if (style == "Quint") return x * x * x * x * x;
        if (style == "Sine") return 1 - std::cos(x * pi / 2);
        if (style == "Back") return x * x * (2.70158 * x - 1.70158);
        if (style == "Bounce") {
            double y = 1 - x;
            if (y < 1 / 2.75) return 1 - 7.5625 * y * y;
            if (y < 2 / 2.75) {
                double z = y - 1.5 / 2.75;
                return 1 - (7.5625 * z * z + 0.75);
            }
            if (y < 2.5 / 2.75) {
                double z = y - 2.25 / 2.75;
                return 1 - (7.5625 * z * z + 0.9375);
            }
            double z = y - 2.625 / 2.75;
            return 1 - (7.5625 * z * z + 0.984375);
        }
        if (style == "Elastic") return x == 0 || x == 1 ? x : -std::pow(2, 10 * x - 10) * std::sin((x * 10 - 10.75) * (2 * pi) / 3);
        return x;
    };
    if (direction == "In") return curve(t);
    if (direction == "InOut") return t < 0.5 ? curve(t * 2) / 2 : 1 - curve((1 - t) * 2) / 2;
    return 1 - curve(1 - t);
}

std::optional<Variant> ScriptTween::mix(const Variant& a, const Variant& b, double t) {
    using K = Variant::Kind;
    auto lerp = [t](double x, double y) { return x + (y - x) * t; };
    if (a.kind == K::number && b.kind == K::number) return Variant::number(lerp(a.numbers[0], b.numbers[0]));
    if (a.kind == b.kind && (a.kind == K::vector3 || a.kind == K::color3 || a.kind == K::vector2 || a.kind == K::udim2 || a.kind == K::cframe)) {
        Variant out = a;
        std::size_t count = a.kind == K::vector2 ? 2 : a.kind == K::udim2 ? 4 : a.kind == K::cframe ? 12 : 3;
        for (std::size_t i = 0; i < count; ++i) out.numbers[i] = lerp(a.numbers[i], b.numbers[i]);
        return out;
    }
    if (t >= 1) return b;
    return std::nullopt;
}

ScriptEngine::ScriptEngine(DataModel& dataModel, std::function<void(const OutputLine&)> sink) : model(dataModel), output(std::move(sink)), implementation(std::make_unique<Implementation>(*this)) {
    model.changed = [this](const InstancePtr& node, const std::string& property) {
        fire(node, "Changed", {Variant::string(implementation->types().limProperty(node->className, property))});
        fire(node, "Changed:" + property, {});
    };
    model.added = [this](const InstancePtr& node) {
        auto parent = node->parent();
        if (!parent) return;
        fire(parent, "ChildAdded", {Variant::object(node)});
        for (auto ancestor = parent; ancestor; ancestor = ancestor->parent()) fire(ancestor, "DescendantAdded", {Variant::object(node)});
    };
    model.removing = [this](const InstancePtr& node) {
        auto parent = node->parent();
        if (!parent) return;
        fire(parent, "ChildRemoved", {Variant::object(node)});
        for (auto ancestor = parent; ancestor; ancestor = ancestor->parent()) fire(ancestor, "DescendantRemoving", {Variant::object(node)});
    };
}

ScriptEngine::~ScriptEngine() {
    stop();
    model.changed = nullptr;
    model.added = nullptr;
    model.removing = nullptr;
}

void ScriptEngine::emit(OutputLine line) {
    if (line.time == 0) line.time = wall();
    if (output) output(line);
}

lim::Vm* ScriptEngine::vm(ScriptContext context) { return implementation->slot(context).vm.get(); }

int ScriptEngine::running() const {
    std::size_t total = 0;
    for (const auto& [context, slot] : implementation->vms) total += slot->vm->running();
    return static_cast<int>(total);
}

ScriptPerformance ScriptEngine::performance() const {
    ScriptPerformance out;
    out.elapsed = time;
    for (const auto& [context, slot] : implementation->vms) {
        out.memory[context] = slot->vm->memoryUsed();
        for (const auto& [label, usage] : slot->vm->scriptUsage())
            out.scripts.push_back({label, context, usage.seconds, usage.events, usage.frames});
    }
    std::sort(out.scripts.begin(), out.scripts.end(), [](const ScriptMetric& a, const ScriptMetric& b) { return a.seconds != b.seconds ? a.seconds > b.seconds : a.script < b.script; });
    return out;
}

void ScriptEngine::launch(const InstancePtr& script, ScriptContext context) {
    ContextVm& slot = implementation->slot(context);
    std::string label = script->path();
    auto result = lim::compileSource(Implementation::source(script), label, implementation->types().registry(), implementation->options(context, label, script));
    if (!result.module) {
        implementation->report(result.diagnostics, label);
        return;
    }
    implementation->report(result.diagnostics, label);
    auto module = std::make_shared<const lim::Module>(std::move(*result.module));
    auto environment = slot.vm->newEnvironment(label, {{"script", implementation->instanceValue(*slot.vm, script)}});
    slot.vm->run(module, environment, label);
}

bool ScriptEngine::execute(const std::string& source, ScriptContext context, const std::string& label) {
    ContextVm& slot = implementation->slot(context);
    lim::AnalyzeOptions options = implementation->options(context, label, nullptr);
    if (!slot.console) slot.console = slot.vm->newEnvironment(label);
    for (const auto& [name, value] : slot.console->values) options.extraGlobals.push_back({name, lim::Type::any()});
    auto result = lim::compileSource(source, label, implementation->types().registry(), options);
    if (!result.module) {
        implementation->report(result.diagnostics, label);
        return false;
    }
    implementation->report(result.diagnostics, label);
    slot.vm->run(std::make_shared<const lim::Module>(std::move(*result.module)), slot.console, label);
    return true;
}

void ScriptEngine::fire(const InstancePtr& node, const std::string& name, const std::vector<Variant>& arguments, std::optional<ScriptContext> context) {
    implementation->fire(node, name, arguments, context);
}

void ScriptEngine::schedule(double after, std::function<void()> action) { timers.emplace_back(time + std::max(0.0, after), std::move(action)); }

void ScriptEngine::step(double delta) {
    time += delta;
    if (auto run = model.service("RunService")) {
        fire(run, "Stepped", {Variant::number(time), Variant::number(delta)});
        fire(run, "Heartbeat", {Variant::number(delta)});
        fire(run, "RenderStepped", {Variant::number(delta)}, ScriptContext::client);
    }
    advanceTweens(delta);
    std::vector<std::function<void()>> due;
    for (auto it = timers.begin(); it != timers.end();) {
        if (it->first <= time) {
            due.push_back(std::move(it->second));
            it = timers.erase(it);
        } else ++it;
    }
    for (auto& action : due) action();
    for (auto& [context, slot] : implementation->vms) slot->vm->step(delta);
}

void ScriptEngine::playTween(const InstancePtr& tween) {
    auto it = implementation->tweenPlans.find(tween->id);
    if (it == implementation->tweenPlans.end()) return;
    auto plan = it->second;
    auto fresh = std::make_shared<ScriptTween>();
    fresh->target = plan->target;
    fresh->goals = plan->goals;
    fresh->duration = plan->duration;
    fresh->style = plan->style;
    fresh->direction = plan->direction;
    for (const auto& [property, goal] : plan->goals)
        if (auto v = model.get(plan->target, property)) fresh->starts[property] = *v;
    std::weak_ptr<LiveInstance> weak = tween;
    fresh->callback = [this, weak] {
        if (auto t = weak.lock()) fire(t, "Completed", {Variant::enumItem("PlaybackState", "Completed")});
    };
    it->second = fresh;
    startTween(fresh);
}

void ScriptEngine::cancelTween(const InstancePtr& tween) {
    auto it = implementation->tweenPlans.find(tween->id);
    if (it == implementation->tweenPlans.end()) return;
    tweens.erase(std::remove(tweens.begin(), tweens.end(), it->second), tweens.end());
}

void ScriptEngine::startTween(std::shared_ptr<ScriptTween> tween) {
    tweens.erase(std::remove_if(tweens.begin(), tweens.end(), [&](const auto& other) {
                     if (other == tween || other->target != tween->target) return false;
                     for (const auto& [key, value] : other->goals)
                         if (tween->goals.count(key)) return true;
                     return false;
                 }),
                 tweens.end());
    tweens.push_back(std::move(tween));
}

void ScriptEngine::advanceTweens(double delta) {
    auto active = tweens;
    for (const auto& tween : active) {
        tween->elapsed += delta;
        double progress = std::min(1.0, tween->elapsed / std::max(0.0001, tween->duration));
        double eased = ScriptTween::ease(progress, tween->style, tween->direction);
        for (const auto& [property, goal] : tween->goals) {
            auto start = tween->starts.find(property);
            if (start == tween->starts.end()) continue;
            if (auto value = ScriptTween::mix(start->second, goal, eased)) model.set(tween->target, property, *value);
        }
        if (progress >= 1) {
            tween->finished = true;
            if (tween->callback) tween->callback();
        }
    }
    tweens.erase(std::remove_if(tweens.begin(), tweens.end(), [](const auto& t) { return t->finished; }), tweens.end());
}

void ScriptEngine::translate(const InstancePtr& node, Vec3 target) {
    std::vector<InstancePtr> parts;
    std::vector<InstancePtr> all{node};
    auto inner = node->descendants();
    all.insert(all.end(), inner.begin(), inner.end());
    for (const auto& n : all)
        if (n->block && n->className != "Model") parts.push_back(n);
    if (parts.empty()) return;
    const WorldBlock& reference = *parts.front()->block;
    double dx = target.x - reference.x, dy = target.y - reference.y, dz = target.z - reference.z;
    for (const auto& n : parts) {
        n->block->x += dx;
        n->block->y += dy;
        n->block->z += dz;
    }
    model.touch();
}

ScriptEngine::Hit ScriptEngine::raycast(Vec3 origin, Vec3 direction, const std::vector<InstancePtr>& ignoring) const {
    double length = direction.length();
    if (length <= 0) return {nullptr, origin, 0};
    Vec3 unit = direction * (1 / length);
    InstancePtr best;
    double distance = length;
    for (const auto& node : model.workspace()->descendants()) {
        if (!node->block || node->className == "Model") continue;
        bool skip = std::any_of(ignoring.begin(), ignoring.end(), [&](const InstancePtr& i) { return node == i || node->isDescendantOf(*i); });
        if (skip) continue;
        if (auto hit = camera_rig::hit(*node->block, origin, unit, distance); hit && *hit < distance) {
            best = node;
            distance = *hit;
        }
    }
    return {best, origin + unit * distance, distance};
}

void ScriptEngine::stop() {
    for (auto& [context, slot] : implementation->vms) slot->vm->stopAll();
    for (auto& [key, signal] : implementation->signals)
        for (auto& c : signal->connections) implementation->disconnect(c);
    implementation->signals.clear();
    tweens.clear();
    timers.clear();
}

}
