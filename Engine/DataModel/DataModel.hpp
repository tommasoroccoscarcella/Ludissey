#pragma once

#include "Engine/Core/Math.hpp"
#include "Engine/Core/Result.hpp"
#include "Engine/Core/Text.hpp"
#include "Engine/DataModel/ClassRegistry.hpp"
#include "Engine/Experience/Experience.hpp"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ld {

class LiveInstance;
using InstancePtr = std::shared_ptr<LiveInstance>;

struct Variant {
    enum class Kind { none, boolean, number, text, vector3, vector2, color3, udim, udim2, cframe, enumItem, instance, brickColor, list, map };
    Kind kind = Kind::none;
    bool flag = false;
    std::array<double, 12> numbers{};
    std::string text;
    std::string type;
    InstancePtr instance;
    std::shared_ptr<const std::vector<Variant>> items;
    std::shared_ptr<const std::vector<std::pair<Variant, Variant>>> entries;

    static Variant none() { return {}; }
    static Variant boolean(bool value) { Variant v; v.kind = Kind::boolean; v.flag = value; return v; }
    static Variant number(double value) { Variant v; v.kind = Kind::number; v.numbers[0] = value; return v; }
    static Variant string(std::string value) { Variant v; v.kind = Kind::text; v.text = std::move(value); return v; }
    static Variant vector3(double x, double y, double z) { Variant v; v.kind = Kind::vector3; v.numbers[0] = x; v.numbers[1] = y; v.numbers[2] = z; return v; }
    static Variant vector2(double x, double y) { Variant v; v.kind = Kind::vector2; v.numbers[0] = x; v.numbers[1] = y; return v; }
    static Variant color3(double r, double g, double b) { Variant v; v.kind = Kind::color3; v.numbers[0] = r; v.numbers[1] = g; v.numbers[2] = b; return v; }
    static Variant udim(double scale, double offset) { Variant v; v.kind = Kind::udim; v.numbers[0] = scale; v.numbers[1] = offset; return v; }
    static Variant udim2(double xs, double xo, double ys, double yo) { Variant v; v.kind = Kind::udim2; v.numbers[0] = xs; v.numbers[1] = xo; v.numbers[2] = ys; v.numbers[3] = yo; return v; }
    static Variant cframe(const std::array<double, 12>& values) { Variant v; v.kind = Kind::cframe; v.numbers = values; return v; }
    static Variant enumItem(std::string type, std::string item) { Variant v; v.kind = Kind::enumItem; v.type = std::move(type); v.text = std::move(item); return v; }
    static Variant object(InstancePtr node) { Variant v; if (node) { v.kind = Kind::instance; v.instance = std::move(node); } return v; }
    static Variant brickColor(std::string hex) { Variant v; v.kind = Kind::brickColor; v.text = std::move(hex); return v; }
    static Variant list(std::vector<Variant> values) { Variant v; v.kind = Kind::list; v.items = std::make_shared<const std::vector<Variant>>(std::move(values)); return v; }
    static Variant map(std::vector<std::pair<Variant, Variant>> values) { Variant v; v.kind = Kind::map; v.entries = std::make_shared<const std::vector<std::pair<Variant, Variant>>>(std::move(values)); return v; }
    static Variant color(std::string_view hex);
    static std::string hex(double r, double g, double b);

    bool operator==(const Variant& other) const;
    bool isNone() const { return kind == Kind::none; }
    double value() const { return numbers[0]; }
};

std::string variantKind(const Variant& value);

class LiveInstance : public std::enable_shared_from_this<LiveInstance> {
public:
    LiveInstance(std::string id, std::string className, std::string name, PropertyMap properties = {}, std::optional<WorldBlock> block = std::nullopt);

    const std::string id;
    const std::string className;
    std::string name;
    std::weak_ptr<LiveInstance> parentLink;
    std::vector<InstancePtr> children;
    PropertyMap properties;
    std::optional<WorldBlock> block;
    Vec3 velocity;
    bool destroyed = false;
    bool archivable = true;
    std::set<std::string> tags;

    InstancePtr parent() const { return parentLink.lock(); }
    std::string path() const;
    bool isDescendantOf(const LiveInstance& ancestor) const;
    std::vector<InstancePtr> descendants() const;
    InstancePtr child(std::string_view name) const;
    const PropertyValue* property(const std::string& key) const;
};

class DataModel {
public:
    static const std::vector<std::string>& specials();
    static const std::vector<std::pair<std::string, std::string>>& materials();

    explicit DataModel(const Experience& experience, const ClassRegistry& registry = ClassRegistry::shared());

    const ClassRegistry& registry;
    InstancePtr game;
    std::function<void(const InstancePtr&, const std::string&)> changed;
    std::function<void(const InstancePtr&)> added;
    std::function<void(const InstancePtr&)> removing;

    const std::map<std::string, InstancePtr>& byId() const { return index; }
    InstancePtr find(std::string_view id) const;
    int revision() const { return revisionCount; }
    void touch() { ++revisionCount; }
    InstancePtr service(std::string_view name) const;
    InstancePtr workspace() const;
    void enroll(const InstancePtr& node);
    Result<InstancePtr> create(const std::string& className);
    InstancePtr make(const std::string& className, std::optional<std::string> name = std::nullopt);
    void adopt(const InstancePtr& node, const InstancePtr& parent);
    Result<void> setParent(const InstancePtr& node, const InstancePtr& parent);
    void destroy(const InstancePtr& node);
    InstancePtr clone(const InstancePtr& node);
    InstancePtr find(const InstancePtr& parent, std::string_view name, bool recursive = false) const;
    std::vector<WorldBlock> parts() const;
    Result<Variant> get(const InstancePtr& node, const std::string& property) const;
    Result<void> set(const InstancePtr& node, const std::string& property, const Variant& value);
    Experience exportInto(const Experience& world) const;
    static std::array<double, 12> frame(const WorldBlock& block);

private:
    std::map<std::string, InstancePtr> index;
    int revisionCount = 0;
    void attach(const InstancePtr& node, const InstancePtr& parent);
    std::optional<Variant> partProperty(const InstancePtr& node, const WorldBlock& block, const std::string& property) const;
    Variant convert(const PropertyValue& value, const PropertyDescriptor& descriptor) const;
    Result<PropertyValue> stored(const Variant& value, const PropertyDescriptor& info) const;
    Result<bool> setPart(const InstancePtr& node, const std::string& property, const Variant& value);
};

}
