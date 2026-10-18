#include "Value.hpp"

#include <cmath>
#include <cstring>

namespace lim {

Object::~Object() {
    if (account && charged) account->fetch_sub(charged);
}

std::size_t ValueHash::operator()(const Value& v) const {
    switch (v.kind) {
    case ValueKind::none: return 0;
    case ValueKind::boolean: return v.flag ? 1 : 2;
    case ValueKind::integer: return std::hash<double>{}(static_cast<double>(v.integer));
    case ValueKind::number: return std::hash<double>{}(v.number);
    case ValueKind::text: return std::hash<std::string>{}(v.as<TextObject>()->text);
    case ValueKind::enumValue: return std::hash<std::string>{}(v.as<EnumValueObject>()->name) ^ 0x9E3779B9;
    case ValueKind::host: return std::hash<std::uint64_t>{}(v.as<HostObject>()->handle);
    case ValueKind::data: {
        const auto* d = v.as<DataObject>();
        std::size_t h = static_cast<std::size_t>(d->kind);
        for (double x : d->values) h = h * 31 + std::hash<double>{}(x);
        return h;
    }
    default: return std::hash<const void*>{}(v.object.get());
    }
}

bool ValueKeyEqual::operator()(const Value& a, const Value& b) const { return valuesEqual(a, b); }

const Value* MapObject::find(const Value& key) const {
    auto it = index.find(key);
    return it == index.end() ? nullptr : &entries[it->second].second;
}

void MapObject::set(const Value& key, Value value) {
    auto it = index.find(key);
    if (it != index.end()) {
        entries[it->second].second = std::move(value);
        return;
    }
    index[key] = entries.size();
    entries.emplace_back(key, std::move(value));
}

bool MapObject::remove(const Value& key) {
    auto it = index.find(key);
    if (it == index.end()) return false;
    std::size_t position = it->second;
    entries.erase(entries.begin() + static_cast<long>(position));
    index.clear();
    for (std::size_t i = 0; i < entries.size(); ++i) index[entries[i].first] = i;
    return true;
}

Value makeData(DataKind kind, std::initializer_list<double> values) {
    auto d = std::make_shared<DataObject>();
    d->kind = kind;
    std::size_t i = 0;
    for (double v : values)
        if (i < d->values.size()) d->values[i++] = v;
    return Value::make(ValueKind::data, d);
}

Value makeVec3(double x, double y, double z) { return makeData(DataKind::vec3, {x, y, z}); }
Value makeVec2(double x, double y) { return makeData(DataKind::vec2, {x, y}); }
Value makeColor(double r, double g, double b) { return makeData(DataKind::color, {r, g, b}); }

Value makeEnum(std::string type, std::string name) {
    auto e = std::make_shared<EnumValueObject>();
    e->type = std::move(type);
    e->name = std::move(name);
    return Value::make(ValueKind::enumValue, e);
}

bool isData(const Value& v, DataKind kind) { return v.kind == ValueKind::data && v.as<DataObject>()->kind == kind; }

std::string_view dataName(DataKind kind) {
    switch (kind) {
    case DataKind::vec2: return "Vec2";
    case DataKind::vec3: return "Vec3";
    case DataKind::color: return "Color";
    case DataKind::rotation: return "Rotation";
    case DataKind::transform: return "Transform";
    case DataKind::ray: return "Ray";
    case DataKind::bounds: return "Bounds";
    case DataKind::dim: return "Dim";
    case DataKind::dim2: return "Dim2";
    }
    return "Data";
}

std::string kindName(const Value& v) {
    switch (v.kind) {
    case ValueKind::none: return "None";
    case ValueKind::boolean: return "Bool";
    case ValueKind::integer: return "Integer";
    case ValueKind::number: return "Number";
    case ValueKind::text: return "Text";
    case ValueKind::list: return "List";
    case ValueKind::map: return "Map";
    case ValueKind::function:
    case ValueKind::native:
    case ValueKind::boundMethod: return "Function";
    case ValueKind::instance: return v.as<InstanceObject>()->type->descriptor.name;
    case ValueKind::classType: return "Class";
    case ValueKind::enumValue: return v.as<EnumValueObject>()->type.empty() ? "Enum" : v.as<EnumValueObject>()->type;
    case ValueKind::enumType: return "Enum";
    case ValueKind::data: return std::string(dataName(v.as<DataObject>()->kind));
    case ValueKind::host: return v.as<HostObject>()->className;
    case ValueKind::task: return "Task";
    case ValueKind::range: return "Range";
    case ValueKind::iterator: return "Iterator";
    case ValueKind::cell: return "Cell";
    case ValueKind::module: return "Module";
    }
    return "Any";
}

bool valuesEqual(const Value& a, const Value& b) {
    if (a.numeric() && b.numeric()) {
        if (a.kind == ValueKind::integer && b.kind == ValueKind::integer) return a.integer == b.integer;
        return a.toNumber() == b.toNumber();
    }
    if (a.kind != b.kind) return false;
    switch (a.kind) {
    case ValueKind::none: return true;
    case ValueKind::boolean: return a.flag == b.flag;
    case ValueKind::text: return a.as<TextObject>()->text == b.as<TextObject>()->text;
    case ValueKind::enumValue: {
        const auto* x = a.as<EnumValueObject>();
        const auto* y = b.as<EnumValueObject>();
        return x->name == y->name && (x->type.empty() || y->type.empty() || x->type == y->type);
    }
    case ValueKind::data: {
        const auto* x = a.as<DataObject>();
        const auto* y = b.as<DataObject>();
        return x->kind == y->kind && x->values == y->values;
    }
    case ValueKind::host: return a.as<HostObject>()->handle == b.as<HostObject>()->handle;
    default: return a.object == b.object;
    }
}

}
