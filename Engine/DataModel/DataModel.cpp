#include "DataModel.hpp"

#include "Engine/Core/Rotation.hpp"
#include "Engine/Core/Rules.hpp"
#include "Engine/Core/Text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace ld {

namespace {

Error failure(std::string message) { return Error::make(ErrorCode::invalidArgument, std::move(message)); }

}

Variant Variant::color(std::string_view hex) {
    unsigned long long value = std::strtoull(std::string(hex).c_str(), nullptr, 16);
    return color3(static_cast<double>((value >> 16) & 255) / 255, static_cast<double>((value >> 8) & 255) / 255, static_cast<double>(value & 255) / 255);
}

std::string Variant::hex(double r, double g, double b) {
    auto channel = [](double value) { return static_cast<int>(std::round(std::max(0.0, std::min(1.0, value)) * 255)); };
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "%02X%02X%02X", channel(r), channel(g), channel(b));
    return buffer;
}

bool Variant::operator==(const Variant& other) const {
    if (kind != other.kind) return false;
    switch (kind) {
    case Kind::none: return true;
    case Kind::boolean: return flag == other.flag;
    case Kind::number: return numbers[0] == other.numbers[0];
    case Kind::text: return text == other.text;
    case Kind::vector3:
    case Kind::color3: return numbers[0] == other.numbers[0] && numbers[1] == other.numbers[1] && numbers[2] == other.numbers[2];
    case Kind::vector2:
    case Kind::udim: return numbers[0] == other.numbers[0] && numbers[1] == other.numbers[1];
    case Kind::udim2: return std::equal(numbers.begin(), numbers.begin() + 4, other.numbers.begin());
    case Kind::cframe: return numbers == other.numbers;
    case Kind::enumItem: return type == other.type && text == other.text;
    case Kind::instance: return instance == other.instance;
    case Kind::brickColor: return text == other.text;
    case Kind::list: return *items == *other.items;
    case Kind::map: return *entries == *other.entries;
    }
    return false;
}

std::string variantKind(const Variant& value) {
    switch (value.kind) {
    case Variant::Kind::none: return "none";
    case Variant::Kind::boolean: return "Bool";
    case Variant::Kind::number: return "Number";
    case Variant::Kind::text: return "Text";
    case Variant::Kind::vector3: return "Vec3";
    case Variant::Kind::vector2: return "Vec2";
    case Variant::Kind::color3: return "Color";
    case Variant::Kind::udim: return "Dim";
    case Variant::Kind::udim2: return "Dim2";
    case Variant::Kind::cframe: return "Transform";
    case Variant::Kind::enumItem: return "EnumItem";
    case Variant::Kind::instance: return "Instance";
    case Variant::Kind::brickColor: return "Color";
    case Variant::Kind::list: return "List";
    case Variant::Kind::map: return "Map";
    }
    return "none";
}

LiveInstance::LiveInstance(std::string identifier, std::string type, std::string title, PropertyMap values, std::optional<WorldBlock> part)
    : id(std::move(identifier)), className(std::move(type)), name(std::move(title)), properties(std::move(values)), block(std::move(part)) {}

std::string LiveInstance::path() const {
    std::vector<std::string> names{name};
    for (auto node = parent(); node && node->className != "DataModel"; node = node->parent()) names.insert(names.begin(), node->name);
    return join(names, ".");
}

bool LiveInstance::isDescendantOf(const LiveInstance& ancestor) const {
    for (auto node = parent(); node; node = node->parent())
        if (node.get() == &ancestor) return true;
    return false;
}

std::vector<InstancePtr> LiveInstance::descendants() const {
    std::vector<InstancePtr> out;
    for (const auto& c : children) {
        out.push_back(c);
        auto inner = c->descendants();
        out.insert(out.end(), inner.begin(), inner.end());
    }
    return out;
}

InstancePtr LiveInstance::child(std::string_view title) const {
    for (const auto& c : children)
        if (c->name == title) return c;
    return nullptr;
}

const PropertyValue* LiveInstance::property(const std::string& key) const {
    auto it = properties.find(key);
    return it == properties.end() ? nullptr : &it->second;
}

const std::vector<std::string>& DataModel::specials() {
    static const std::vector<std::string> names = {"RunService", "UserInputService", "Debris", "TweenService", "HttpService", "Selection"};
    return names;
}

const std::vector<std::pair<std::string, std::string>>& DataModel::materials() {
    static const std::vector<std::pair<std::string, std::string>> table = {{"plastic", "Plastic"}, {"wood", "Wood"}, {"metal", "Metal"}, {"neon", "Neon"}, {"glass", "Glass"}};
    return table;
}

DataModel::DataModel(const Experience& experience, const ClassRegistry& classes) : registry(classes) {
    game = std::make_shared<LiveInstance>("game", "DataModel", experience.title);
    index[game->id] = game;
    for (const auto& service : registry.services()) attach(std::make_shared<LiveInstance>(service.name, service.name, service.name), game);
    for (const auto& special : specials()) {
        auto node = std::make_shared<LiveInstance>(special, special, special);
        node->parentLink = game;
        index[special] = node;
    }
    std::vector<std::pair<InstancePtr, std::string>> pending;
    for (const auto& block : experience.blocks) {
        auto node = std::make_shared<LiveInstance>(block.id, block.className(), block.name.empty() ? block.className() : block.name, block.properties, block);
        index[node->id] = node;
        pending.emplace_back(node, block.parent);
    }
    for (const auto& record : experience.instances) {
        PropertyMap merged = registry.defaults(record.className);
        for (const auto& [key, value] : record.properties) merged[key] = value;
        auto node = std::make_shared<LiveInstance>(record.id, record.className, record.name, merged);
        index[node->id] = node;
        pending.emplace_back(node, record.parent);
    }
    for (const auto& child : registry.defaultChildren()) {
        const std::string& className = child.first;
        const std::string& parent = child.second;
        bool present = std::any_of(experience.instances.begin(), experience.instances.end(), [&](const InstanceRecord& r) { return r.className == className && r.parent == parent; });
        if (present) continue;
        auto node = std::make_shared<LiveInstance>(parent + "." + className, className, className);
        index[node->id] = node;
        pending.emplace_back(node, parent);
    }
    for (const auto& [node, parent] : pending) {
        auto owner = find(parent);
        if (owner && !owner->isDescendantOf(*node) && owner != node) attach(node, owner);
    }
}

void DataModel::attach(const InstancePtr& node, const InstancePtr& parent) {
    node->parentLink = parent;
    parent->children.push_back(node);
    index[node->id] = node;
}

InstancePtr DataModel::find(std::string_view id) const {
    auto it = index.find(std::string(id));
    return it == index.end() ? nullptr : it->second;
}

InstancePtr DataModel::service(std::string_view name) const {
    auto node = find(name);
    if (!node) return nullptr;
    bool special = std::find(specials().begin(), specials().end(), name) != specials().end();
    if (node->parent() != game && !special) return nullptr;
    return node;
}

InstancePtr DataModel::workspace() const { return find("Workspace"); }

void DataModel::enroll(const InstancePtr& node) {
    index[node->id] = node;
    for (const auto& c : node->children) enroll(c);
}

Result<InstancePtr> DataModel::create(const std::string& className) {
    const ClassDescriptor* descriptor = registry.find(className);
    if (!descriptor || !descriptor->canCreate()) return failure("Non si può creare un'istanza di tipo «" + className + "»");
    std::string id = makeUuid();
    std::optional<WorldBlock> block;
    if (descriptor->kind) {
        WorldBlock part;
        part.id = id;
        part.kind = *descriptor->kind;
        part.name = className;
        part.parent = "";
        if (part.kind == "model") part.sizeX = part.sizeY = part.sizeZ = 1;
        if (part.kind == "cube" || part.kind == "wedge" || part.kind == "seat" || part.kind == "spawn") {
            part.sizeX = 4;
            part.sizeY = 1.2;
            part.sizeZ = 2;
        }
        block = part;
    }
    auto node = std::make_shared<LiveInstance>(id, className, className, registry.defaults(className), block);
    index[node->id] = node;
    return node;
}

InstancePtr DataModel::make(const std::string& className, std::optional<std::string> name) {
    auto node = std::make_shared<LiveInstance>(makeUuid(), className, name.value_or(className), registry.defaults(className));
    index[node->id] = node;
    return node;
}

void DataModel::adopt(const InstancePtr& node, const InstancePtr& parent) {
    if (auto old = node->parent()) old->children.erase(std::remove(old->children.begin(), old->children.end(), node), old->children.end());
    node->parentLink = parent;
    parent->children.push_back(node);
    enroll(node);
    if (added) added(node);
    touch();
}

Result<void> DataModel::setParent(const InstancePtr& node, const InstancePtr& parent) {
    if (!node) return failure("Istanza non valida");
    if (node->destroyed) return failure("Il genitore di «" + node->name + "» è bloccato: l'istanza è stata distrutta");
    if (find(node->id) != node) return failure("L'istanza non appartiene a questo mondo");
    if (registry.isService(node->className) || node == game) return failure("Il genitore di «" + node->name + "» è bloccato");
    if (parent) {
        if (parent->destroyed || find(parent->id) != parent) return failure("Il genitore non è disponibile in questo mondo");
        if (parent == node || parent->isDescendantOf(*node)) return failure("«" + node->path() + "» non può diventare genitore di se stesso");
        if (parent->className != "DataModel" && !registry.canParent(node->className, parent->className)) return failure(node->className + " non può essere inserito in " + parent->className);
    }
    if (node->parent() == parent) return {};
    if (auto old = node->parent()) {
        if (removing) removing(node);
        old->children.erase(std::remove(old->children.begin(), old->children.end(), node), old->children.end());
    }
    node->parentLink = parent;
    if (parent) {
        parent->children.push_back(node);
        enroll(node);
        if (added) added(node);
    }
    touch();
    if (changed) changed(node, "Parent");
    return {};
}

void DataModel::destroy(const InstancePtr& node) {
    if (registry.isService(node->className) || node == game) return;
    auto children = node->children;
    for (const auto& c : children) destroy(c);
    setParent(node, nullptr);
    node->destroyed = true;
    index.erase(node->id);
}

InstancePtr DataModel::clone(const InstancePtr& node) {
    if (!node->archivable || registry.isService(node->className) || node == game) return nullptr;
    auto block = node->block;
    if (block) block->id = makeUuid();
    auto copy = std::make_shared<LiveInstance>(block ? block->id : makeUuid(), node->className, node->name, node->properties, block);
    copy->tags = node->tags;
    index[copy->id] = copy;
    for (const auto& c : node->children) {
        auto duplicate = clone(c);
        if (!duplicate) continue;
        duplicate->parentLink = copy;
        copy->children.push_back(duplicate);
    }
    return copy;
}

InstancePtr DataModel::find(const InstancePtr& parent, std::string_view name, bool recursive) const {
    if (auto direct = parent->child(name)) return direct;
    if (!recursive) return nullptr;
    for (const auto& c : parent->children)
        if (auto found = find(c, name, true)) return found;
    return nullptr;
}

std::vector<WorldBlock> DataModel::parts() const {
    std::vector<WorldBlock> out;
    for (const auto& node : workspace()->descendants()) {
        if (!node->block || node->className == "Model") continue;
        WorldBlock block = *node->block;
        block.id = node->id;
        block.name = node->name;
        auto parent = node->parent();
        block.parent = parent ? parent->id : "";
        out.push_back(block);
    }
    return out;
}

std::array<double, 12> DataModel::frame(const WorldBlock& block) {
    Mat3 m = block.rotationMatrix();
    return {block.x, block.y, block.z, m[0][0], m[0][1], m[0][2], m[1][0], m[1][1], m[1][2], m[2][0], m[2][1], m[2][2]};
}

Result<Variant> DataModel::get(const InstancePtr& node, const std::string& property) const {
    if (property == "Name") return Variant::string(node->name);
    if (property == "ClassName" || property == "className") return Variant::string(node->className);
    if (property == "Parent") return Variant::object(node->parent());
    if (property == "Archivable" || property == "archivable") return Variant::boolean(node->archivable);
    if (node->block)
        if (auto value = partProperty(node, *node->block, property)) return *value;
    const ClassDescriptor* descriptor = registry.find(node->className);
    if (!descriptor || !descriptor->properties.count(property)) return failure("«" + property + "» non è un membro di " + node->className);
    const PropertyDescriptor& info = descriptor->properties.at(property);
    if (const PropertyValue* stored = node->property(property)) return convert(*stored, info);
    PropertyMap defaults = registry.defaults(node->className);
    if (auto it = defaults.find(property); it != defaults.end()) return convert(it->second, info);
    return Variant::none();
}

std::optional<Variant> DataModel::partProperty(const InstancePtr& node, const WorldBlock& block, const std::string& property) const {
    if (node->className == "Model") {
        if (property == "PrimaryPart") {
            const PropertyValue* ref = node->property("PrimaryPart");
            auto id = ref ? ref->referenceId() : std::nullopt;
            return Variant::object(id ? find(*id) : nullptr);
        }
        return std::nullopt;
    }
    if (property == "Position" || property == "position") return Variant::vector3(block.x, block.y, block.z);
    if (property == "Size" || property == "size") return Variant::vector3(block.sizeX, block.sizeY, block.sizeZ);
    if (property == "Orientation" || property == "Rotation") return Variant::vector3(block.rotationX, block.rotationY, block.rotationZ);
    if (property == "CFrame") return Variant::cframe(frame(block));
    if (property == "Color") return Variant::color(block.color);
    if (property == "BrickColor") return Variant::brickColor(block.color);
    if (property == "Material") {
        std::string name = "Plastic";
        for (const auto& [ours, dataset] : materials())
            if (ours == block.material) name = dataset;
        return Variant::enumItem("Material", name);
    }
    if (property == "Transparency") return Variant::number(block.transparency);
    if (property == "Reflectance") return Variant::number(block.reflectance);
    if (property == "Anchored") return Variant::boolean(block.anchored);
    if (property == "CanCollide") return Variant::boolean(block.canCollide);
    if (property == "Locked") return Variant::boolean(block.locked);
    if (property == "Velocity") return Variant::vector3(node->velocity.x, node->velocity.y, node->velocity.z);
    if (property == "Shape") return Variant::enumItem("PartType", block.kind == "sphere" ? "Ball" : block.kind == "cylinder" ? "Cylinder" : "Block");
    if (property == "MeshId") return Variant::string(block.meshAsset ? std::string(rules::assets::prefix) + *block.meshAsset : "");
    return std::nullopt;
}

Variant DataModel::convert(const PropertyValue& value, const PropertyDescriptor& descriptor) const {
    using K = PropertyValue::Kind;
    switch (value.kind) {
    case K::string:
    case K::content: return Variant::string(value.text);
    case K::number: return Variant::number(value.numbers[0]);
    case K::boolean: return Variant::boolean(value.flag);
    case K::vector3: return Variant::vector3(value.numbers[0], value.numbers[1], value.numbers[2]);
    case K::vector2: return Variant::vector2(value.numbers[0], value.numbers[1]);
    case K::color3: return descriptor.typeName == "BrickColor" ? Variant::brickColor(value.text) : Variant::color(value.text);
    case K::udim: return Variant::udim(value.numbers[0], value.numbers[1]);
    case K::udim2: return Variant::udim2(value.numbers[0], value.numbers[1], value.numbers[2], value.numbers[3]);
    case K::enumItem: return Variant::enumItem(descriptor.typeName, value.text);
    case K::reference: {
        auto id = value.referenceId();
        return Variant::object(id ? find(*id) : nullptr);
    }
    }
    return Variant::none();
}

Result<void> DataModel::set(const InstancePtr& node, const std::string& property, const Variant& value) {
    if (property == "Parent") {
        if (value.kind == Variant::Kind::instance) return setParent(node, value.instance);
        if (value.isNone()) return setParent(node, nullptr);
        return failure("parent deve essere un'istanza o none");
    }
    if (property == "Name") {
        if (value.kind != Variant::Kind::text) return failure("name deve essere un testo");
        node->name = utf8Prefix(value.text, rules::instances::nameCharacters);
        if (node->block) node->block->name = node->name;
        touch();
        if (changed) changed(node, property);
        return {};
    }
    if (property == "Archivable") {
        if (value.kind == Variant::Kind::boolean) node->archivable = value.flag;
        return {};
    }
    if (node->block) {
        auto handled = setPart(node, property, value);
        if (!handled) return handled.error();
        if (*handled) {
            touch();
            if (changed) changed(node, property);
            return {};
        }
    }
    const ClassDescriptor* descriptor = registry.find(node->className);
    if (!descriptor || !descriptor->properties.count(property)) return failure("«" + property + "» non è un membro di " + node->className);
    const PropertyDescriptor& info = descriptor->properties.at(property);
    if (info.readOnly) return failure("La proprietà «" + property + "» è di sola lettura");
    auto converted = stored(value, info);
    if (!converted) return converted.error();
    node->properties[property] = *converted;
    touch();
    if (changed) changed(node, property);
    return {};
}

Result<PropertyValue> DataModel::stored(const Variant& value, const PropertyDescriptor& info) const {
    using K = Variant::Kind;
    auto mismatch = [&] { return failure("Serve " + info.typeName + ", trovato " + variantKind(value)); };
    if (info.category == "Primitive" && info.typeName == "bool") {
        if (value.kind != K::boolean) return mismatch();
        return PropertyValue::boolean(value.flag);
    }
    if (info.category == "Primitive" && info.typeName == "string") {
        if (value.kind == K::text) return PropertyValue::string(utf8Prefix(value.text, rules::instances::textCharacters));
        if (value.kind == K::number) return PropertyValue::string(doubleText(value.value()));
        return mismatch();
    }
    if (info.category == "Primitive") {
        if (value.kind == K::number) return PropertyValue::number(info.typeName == "int" ? std::trunc(value.value()) : value.value());
        if (value.kind == K::text) {
            if (auto number = parseNumber(value.text)) return PropertyValue::number(*number);
        }
        return mismatch();
    }
    if (info.category == "DataType") {
        if (info.typeName == "Content") {
            if (value.kind != K::text) return mismatch();
            return PropertyValue::content(value.text);
        }
        if (info.typeName == "Vector3") {
            if (value.kind != K::vector3) return mismatch();
            return PropertyValue::vector3(value.numbers[0], value.numbers[1], value.numbers[2]);
        }
        if (info.typeName == "Vector2") {
            if (value.kind != K::vector2) return mismatch();
            return PropertyValue::vector2(value.numbers[0], value.numbers[1]);
        }
        if (info.typeName == "Color3") {
            if (value.kind != K::color3) return mismatch();
            return PropertyValue::color3(Variant::hex(value.numbers[0], value.numbers[1], value.numbers[2]));
        }
        if (info.typeName == "BrickColor") {
            if (value.kind == K::brickColor) return PropertyValue::color3(value.text);
            if (value.kind == K::color3) return PropertyValue::color3(Variant::hex(value.numbers[0], value.numbers[1], value.numbers[2]));
            return mismatch();
        }
        if (info.typeName == "UDim") {
            if (value.kind != K::udim) return mismatch();
            return PropertyValue::udim(value.numbers[0], value.numbers[1]);
        }
        if (info.typeName == "UDim2") {
            if (value.kind != K::udim2) return mismatch();
            return PropertyValue::udim2(value.numbers[0], value.numbers[1], value.numbers[2], value.numbers[3]);
        }
    }
    if (info.category == "Enum") {
        const auto& enums = registry.enums();
        auto items = enums.find(info.typeName);
        if (value.kind == K::enumItem && value.type == info.typeName) return PropertyValue::enumItem(value.text);
        if (value.kind == K::text && items != enums.end() && std::find(items->second.begin(), items->second.end(), value.text) != items->second.end()) return PropertyValue::enumItem(value.text);
        if (value.kind == K::number && items != enums.end()) {
            int position = static_cast<int>(value.value());
            if (position >= 0 && position < static_cast<int>(items->second.size())) return PropertyValue::enumItem(items->second[static_cast<std::size_t>(position)]);
        }
        return mismatch();
    }
    if (info.category == "Class") {
        if (value.kind == K::instance) return PropertyValue::reference(value.instance->id);
        if (value.isNone()) return PropertyValue::reference(std::nullopt);
        return mismatch();
    }
    return failure("La proprietà «" + info.name + "» non è modificabile dagli script in Ludissey");
}

Result<bool> DataModel::setPart(const InstancePtr& node, const std::string& property, const Variant& value) {
    using K = Variant::Kind;
    WorldBlock block = *node->block;
    if (node->className == "Model") {
        if (property != "PrimaryPart") return false;
        node->properties["PrimaryPart"] = PropertyValue::reference(value.kind == K::instance ? std::optional<std::string>(value.instance->id) : std::nullopt);
        return true;
    }
    auto finite = [](double v) { return std::isfinite(v); };
    auto vector = [&]() -> Result<Vec3> {
        if (value.kind != K::vector3 || !finite(value.numbers[0]) || !finite(value.numbers[1]) || !finite(value.numbers[2])) return failure("Serve Vec3, trovato " + variantKind(value));
        return Vec3{value.numbers[0], value.numbers[1], value.numbers[2]};
    };
    auto number = [&]() -> Result<double> {
        if (value.kind != K::number || !finite(value.value())) return failure("Serve Number, trovato " + variantKind(value));
        return value.value();
    };
    auto flag = [&]() -> Result<bool> {
        if (value.kind != K::boolean) return failure("Serve Bool, trovato " + variantKind(value));
        return value.flag;
    };
    const double limit = rules::world::maximumSize, minimum = rules::world::minimumSize;
    if (property == "Position" || property == "position") {
        auto v = vector();
        if (!v) return v.error();
        block.x = v->x;
        block.y = v->y;
        block.z = v->z;
    } else if (property == "Size" || property == "size") {
        auto v = vector();
        if (!v) return v.error();
        block.sizeX = std::min(limit, std::max(minimum, v->x));
        block.sizeY = std::min(limit, std::max(minimum, v->y));
        block.sizeZ = std::min(limit, std::max(minimum, v->z));
    } else if (property == "Orientation" || property == "Rotation") {
        auto v = vector();
        if (!v) return v.error();
        block.rotationX = v->x;
        block.rotationY = v->y;
        block.rotationZ = v->z;
    } else if (property == "CFrame") {
        if (value.kind != K::cframe) return failure("Serve Transform, trovato " + variantKind(value));
        const auto& m = value.numbers;
        block.x = m[0];
        block.y = m[1];
        block.z = m[2];
        Mat3 rotation;
        rotation.m = {{{m[3], m[4], m[5]}, {m[6], m[7], m[8]}, {m[9], m[10], m[11]}}};
        Vec3 angles = rotation::euler(rotation);
        block.rotationX = angles.x;
        block.rotationY = angles.y;
        block.rotationZ = angles.z;
    } else if (property == "Color") {
        if (value.kind != K::color3) return failure("Serve Color, trovato " + variantKind(value));
        block.color = Variant::hex(value.numbers[0], value.numbers[1], value.numbers[2]);
    } else if (property == "BrickColor") {
        if (value.kind == K::brickColor) block.color = value.text;
        else if (value.kind == K::color3) block.color = Variant::hex(value.numbers[0], value.numbers[1], value.numbers[2]);
        else return failure("Serve Color, trovato " + variantKind(value));
    } else if (property == "Material") {
        std::string name;
        if (value.kind == K::enumItem && value.type == "Material") name = value.text;
        else if (value.kind == K::text) name = value.text;
        else return failure("Serve Material, trovato " + variantKind(value));
        auto match = std::find_if(materials().begin(), materials().end(), [&](const auto& m) { return m.second == name; });
        if (match == materials().end()) return failure("Materiale non disponibile in Ludissey: " + name);
        block.material = match->first;
    } else if (property == "Transparency") {
        auto n = number();
        if (!n) return n.error();
        block.transparency = std::max(0.0, std::min(1.0, *n));
    } else if (property == "Reflectance") {
        auto n = number();
        if (!n) return n.error();
        block.reflectance = std::max(0.0, std::min(1.0, *n));
    } else if (property == "Anchored" || property == "CanCollide" || property == "Locked") {
        auto f = flag();
        if (!f) return f.error();
        (property == "Anchored" ? block.anchored : property == "CanCollide" ? block.canCollide : block.locked) = *f;
    } else if (property == "Velocity") {
        auto v = vector();
        if (!v) return v.error();
        node->velocity = *v;
    } else if (property == "Shape") {
        if (node->className != "Part") return false;
        if (value.kind != K::enumItem || value.type != "PartType") return failure("Serve PartType, trovato " + variantKind(value));
        block.kind = value.text == "Ball" ? "sphere" : value.text == "Cylinder" ? "cylinder" : "cube";
    } else {
        return false;
    }
    node->block = block;
    return true;
}

Experience DataModel::exportInto(const Experience& world) const {
    Experience result = world;
    std::map<std::string, WorldBlock> blocks;
    std::map<std::string, InstanceRecord> records;
    std::vector<std::string> order;
    std::set<std::string> implicit;
    for (const auto& [className, parent] : registry.defaultChildren()) implicit.insert(parent + "." + className);
    std::map<std::string, std::string> named;
    for (const auto& b : world.blocks) named[b.id] = b.name;
    std::map<std::string, const InstanceRecord*> originals;
    for (const auto& r : world.instances) originals[r.id] = &r;
    std::function<void(const InstancePtr&)> visit = [&](const InstancePtr& node) {
        for (const auto& c : node->children) {
            if (node != game) {
                if (c->block) {
                    WorldBlock block = *c->block;
                    block.id = c->id;
                    auto known = named.find(c->id);
                    block.name = known != named.end() && known->second.empty() && c->name == c->className ? "" : c->name;
                    block.parent = node->id;
                    block.properties = c->properties;
                    blocks[c->id] = block;
                    order.push_back(c->id);
                } else if (!implicit.count(c->id)) {
                    PropertyMap defaults = registry.defaults(c->className);
                    auto original = originals.find(c->id);
                    PropertyMap properties;
                    for (const auto& [key, value] : c->properties) {
                        bool kept = original != originals.end() && original->second->properties.count(key);
                        auto d = defaults.find(key);
                        if (kept || d == defaults.end() || !(d->second == value)) properties[key] = value;
                    }
                    records[c->id] = InstanceRecord{c->id, c->className, c->name, node->id, properties};
                    order.push_back(c->id);
                }
            }
            visit(c);
        }
    };
    visit(game);
    std::set<std::string> known;
    for (const auto& b : world.blocks) known.insert(b.id);
    for (const auto& r : world.instances) known.insert(r.id);
    result.blocks.clear();
    for (const auto& b : world.blocks)
        if (auto it = blocks.find(b.id); it != blocks.end()) result.blocks.push_back(it->second);
    for (const auto& id : order)
        if (!known.count(id))
            if (auto it = blocks.find(id); it != blocks.end()) result.blocks.push_back(it->second);
    result.instances.clear();
    for (const auto& r : world.instances)
        if (auto it = records.find(r.id); it != records.end()) result.instances.push_back(it->second);
    for (const auto& id : order)
        if (!known.count(id))
            if (auto it = records.find(id); it != records.end()) result.instances.push_back(it->second);
    return result;
}

}
