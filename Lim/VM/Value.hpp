#pragma once

#include "Lim/Bytecode/Bytecode.hpp"

#include <array>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace lim {

class Vm;
struct Fiber;
struct Environment;

enum class ValueKind : std::uint8_t { none, boolean, integer, number, text, list, map, function, native, instance, classType, enumValue, enumType, data, host, task, range, iterator, cell, module, boundMethod };

enum class DataKind : std::uint8_t { vec2, vec3, color, rotation, transform, ray, bounds, dim, dim2 };

struct Object {
    virtual ~Object();
    std::shared_ptr<std::atomic<long long>> account;
    long long charged = 0;
    virtual void release() {}
};

struct Value {
    ValueKind kind = ValueKind::none;
    union {
        bool flag;
        long long integer;
        double number;
    };
    std::shared_ptr<Object> object;

    Value() : integer(0) {}
    static Value boolean(bool b) { Value v; v.kind = ValueKind::boolean; v.flag = b; return v; }
    static Value from(long long i) { Value v; v.kind = ValueKind::integer; v.integer = i; return v; }
    static Value real(double n) { Value v; v.kind = ValueKind::number; v.number = n; return v; }
    static Value make(ValueKind kind, std::shared_ptr<Object> object) { Value v; v.kind = kind; v.object = std::move(object); return v; }
    bool isNone() const { return kind == ValueKind::none; }
    bool numeric() const { return kind == ValueKind::integer || kind == ValueKind::number; }
    double toNumber() const { return kind == ValueKind::integer ? static_cast<double>(integer) : kind == ValueKind::number ? number : 0; }
    template <typename T>
    T* as() const { return static_cast<T*>(object.get()); }
};

struct TextObject : Object {
    std::string text;
};

struct ListObject : Object {
    std::vector<Value> items;
    void release() override { items.clear(); }
};

struct ValueHash {
    std::size_t operator()(const Value& v) const;
};

struct ValueKeyEqual {
    bool operator()(const Value& a, const Value& b) const;
};

struct MapObject : Object {
    std::vector<std::pair<Value, Value>> entries;
    std::unordered_map<Value, std::size_t, ValueHash, ValueKeyEqual> index;
    const Value* find(const Value& key) const;
    void set(const Value& key, Value value);
    bool remove(const Value& key);
    void release() override { entries.clear(); index.clear(); }
};

struct CellObject : Object {
    Value value;
    void release() override { value = Value(); }
};

struct ClosureObject : Object {
    std::shared_ptr<const Module> module;
    std::uint32_t prototype = 0;
    std::vector<std::shared_ptr<CellObject>> cells;
    std::shared_ptr<Environment> environment;
    std::string label;
    const Prototype& proto() const { return module->prototypes[prototype]; }
    void release() override;
};

struct NativeCall {
    Vm& vm;
    std::span<const Value> arguments;
    const std::vector<std::string>* names = nullptr;
    Fiber* fiber = nullptr;
    const Value& argument(std::size_t i) const;
    std::size_t count() const { return arguments.size(); }
    const Value* named(std::string_view name) const;
    const Value& parameter(std::size_t position, std::string_view name) const;
};

struct Suspension {
    enum class Kind { none, time, frame, task, event, poll } kind = Kind::none;
    double until = 0;
    Value target;
    std::function<std::optional<Value>()> poll;
    double timeout = -1;
};

struct NativeResult {
    Value value;
    Suspension suspension;
    static NativeResult of(Value v) { NativeResult r; r.value = std::move(v); return r; }
};

using NativeFunction = std::function<NativeResult(NativeCall&)>;

struct NativeObject : Object {
    std::string name;
    NativeFunction function;
    Value receiver;
    bool async = false;
    std::map<std::string, Value> members;
};

struct ClassObject : Object {
    std::shared_ptr<const Module> module;
    ClassDescriptor descriptor;
    std::map<std::string, std::size_t> fieldIndex;
    std::map<std::string, std::uint32_t> methodIndex;
};

struct InstanceObject : Object {
    std::shared_ptr<ClassObject> type;
    std::vector<Value> fields;
    void release() override { fields.clear(); }
};

struct EnumTypeObject : Object {
    std::string name;
    std::vector<std::string> cases;
};

struct EnumValueObject : Object {
    std::string type;
    std::string name;
};

struct DataObject : Object {
    DataKind kind = DataKind::vec3;
    std::array<double, 12> values{};
};

struct HostObject : Object {
    std::uint64_t handle = 0;
    std::string className;
    std::string tag;
};

struct TaskObject : Object {
    bool done = false;
    bool failed = false;
    bool cancelled = false;
    Value result;
    std::string error;
    std::weak_ptr<Fiber> fiber;
    std::vector<std::weak_ptr<Fiber>> waiters;
};

struct RangeObject : Object {
    long long low = 0;
    long long high = 0;
    bool inclusive = true;
};

struct IteratorObject : Object {
    Value source;
    std::size_t position = 0;
    long long current = 0;
    bool exhausted = false;
    std::vector<std::uint32_t> characters;
};

struct ModuleObject : Object {
    std::string name;
    std::map<std::string, Value> exports;
    void release() override { exports.clear(); }
};

struct BoundMethodObject : Object {
    Value receiver;
    Value function;
    void release() override { receiver = Value(); function = Value(); }
};

class ScriptError : public std::runtime_error {
public:
    explicit ScriptError(const std::string& message) : std::runtime_error(message) {}
};

Value makeData(DataKind kind, std::initializer_list<double> values);
Value makeVec3(double x, double y, double z);
Value makeVec2(double x, double y);
Value makeColor(double r, double g, double b);
Value makeEnum(std::string type, std::string name);
bool isData(const Value& v, DataKind kind);
std::string_view dataName(DataKind kind);
std::string kindName(const Value& v);
bool valuesEqual(const Value& a, const Value& b);

}
