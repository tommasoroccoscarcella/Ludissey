#include "GuiLayout.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ld {

Rect Rect::intersection(const Rect& other) const {
    if (null || other.null) return none();
    double left = std::max(minX(), other.minX()), top = std::max(minY(), other.minY());
    double right = std::min(maxX(), other.maxX()), bottom = std::min(maxY(), other.maxY());
    if (right < left || bottom < top) return none();
    return make(left, top, right - left, bottom - top);
}

bool Rect::operator==(const Rect& other) const {
    if (null || other.null) return null == other.null;
    return minX() == other.minX() && minY() == other.minY() && width() == other.width() && height() == other.height();
}

double GuiNode::number(const std::string& key, double standard) const {
    auto it = properties.find(key);
    return it != properties.end() && it->second.kind == PropertyValue::Kind::number ? it->second.numbers[0] : standard;
}

bool GuiNode::flag(const std::string& key, bool standard) const {
    auto it = properties.find(key);
    return it != properties.end() && it->second.kind == PropertyValue::Kind::boolean ? it->second.flag : standard;
}

std::string GuiNode::text(const std::string& key) const {
    auto it = properties.find(key);
    if (it == properties.end()) return "";
    return it->second.kind == PropertyValue::Kind::string || it->second.kind == PropertyValue::Kind::content ? it->second.text : "";
}

std::string GuiNode::color(const std::string& key, const std::string& standard) const {
    auto it = properties.find(key);
    return it != properties.end() && it->second.kind == PropertyValue::Kind::color3 ? it->second.text : standard;
}

std::string GuiNode::item(const std::string& key, const std::string& standard) const {
    auto it = properties.find(key);
    return it != properties.end() && it->second.kind == PropertyValue::Kind::enumItem ? it->second.text : standard;
}

std::array<double, 4> GuiNode::udim2(const std::string& key) const {
    auto it = properties.find(key);
    if (it != properties.end() && it->second.kind == PropertyValue::Kind::udim2) return it->second.numbers;
    return {0, 0, 0, 0};
}

std::array<double, 2> GuiNode::udim(const std::string& key) const {
    auto it = properties.find(key);
    if (it != properties.end() && it->second.kind == PropertyValue::Kind::udim) return {it->second.numbers[0], it->second.numbers[1]};
    return {0, 0};
}

std::array<double, 2> GuiNode::vector2(const std::string& key) const {
    auto it = properties.find(key);
    if (it != properties.end() && it->second.kind == PropertyValue::Kind::vector2) return {it->second.numbers[0], it->second.numbers[1]};
    return {0, 0};
}

std::string_view roleName(GuiElement::Role role) {
    switch (role) {
    case GuiElement::Role::frame: return "frame";
    case GuiElement::Role::label: return "label";
    case GuiElement::Role::button: return "button";
    case GuiElement::Role::box: return "box";
    case GuiElement::Role::image: return "image";
    case GuiElement::Role::imageButton: return "imageButton";
    case GuiElement::Role::scroller: return "scroller";
    }
    return "frame";
}

namespace gui_snapshot {

const std::vector<std::string>& visual() {
    static const std::vector<std::string> names = {"Frame", "ScrollingFrame", "TextLabel", "TextButton", "TextBox", "ImageLabel", "ImageButton", "UIListLayout", "UIGridLayout", "UIPadding", "ScreenGui", "Folder"};
    return names;
}

namespace {

bool isVisual(const std::string& className) { return std::find(visual().begin(), visual().end(), className) != visual().end(); }

PropertyMap merged(const ClassRegistry& registry, const std::string& className, const PropertyMap& properties) {
    PropertyMap out = registry.defaults(className);
    for (const auto& [key, value] : properties) out[key] = value;
    return out;
}

}

std::vector<GuiNode> capture(const InstancePtr& root, const ClassRegistry& registry) {
    std::vector<GuiNode> out;
    if (!root) return out;
    std::function<std::optional<GuiNode>(const InstancePtr&)> node = [&](const InstancePtr& instance) -> std::optional<GuiNode> {
        if (!isVisual(instance->className)) return std::nullopt;
        GuiNode n{instance->id, instance->className, instance->name, merged(registry, instance->className, instance->properties), {}};
        for (const auto& c : instance->children)
            if (auto child = node(c)) n.children.push_back(std::move(*child));
        return n;
    };
    for (const auto& c : root->children)
        if (auto n = node(c); n && n->className == "ScreenGui") out.push_back(std::move(*n));
    return out;
}

std::vector<GuiNode> capture(const Experience& world, const std::string& parent, const ClassRegistry& registry) {
    std::function<std::optional<GuiNode>(const InstanceRecord&)> node = [&](const InstanceRecord& record) -> std::optional<GuiNode> {
        if (!isVisual(record.className)) return std::nullopt;
        GuiNode n{record.id, record.className, record.name, merged(registry, record.className, record.properties), {}};
        for (const auto& r : world.instances)
            if (r.parent == record.id)
                if (auto child = node(r)) n.children.push_back(std::move(*child));
        return n;
    };
    std::vector<GuiNode> out;
    for (const auto& r : world.instances)
        if (r.parent == parent)
            if (auto n = node(r); n && n->className == "ScreenGui") out.push_back(std::move(*n));
    return out;
}

}

namespace gui_layout {

namespace {

using Key = std::vector<double>;

struct Placed {
    Key key;
    GuiElement element;
};

GuiElement describe(const GuiNode& node, const Rect& frame, const Rect& clip) {
    GuiElement::Role role = GuiElement::Role::frame;
    if (node.className == "TextLabel") role = GuiElement::Role::label;
    else if (node.className == "TextButton") role = GuiElement::Role::button;
    else if (node.className == "TextBox") role = GuiElement::Role::box;
    else if (node.className == "ImageLabel") role = GuiElement::Role::image;
    else if (node.className == "ImageButton") role = GuiElement::Role::imageButton;
    else if (node.className == "ScrollingFrame") role = GuiElement::Role::scroller;
    GuiElement e;
    e.id = node.id;
    e.role = role;
    e.frame = frame;
    e.clip = clip;
    e.background = node.color("BackgroundColor3", "FFFFFF");
    e.backgroundAlpha = 1 - node.number("BackgroundTransparency");
    e.border = node.color("BorderColor3", "1B2A35");
    e.borderSize = node.number("BorderSizePixel", 1);
    e.text = node.text("Text");
    e.hint = node.text("PlaceholderText");
    e.textColor = node.color("TextColor3", "1B2A35");
    e.textAlpha = 1 - node.number("TextTransparency");
    e.textSize = node.number("TextSize", 14);
    e.font = node.item("Font", "SourceSans");
    e.scaled = node.flag("TextScaled", false);
    e.wrapped = node.flag("TextWrapped", false);
    e.horizontal = node.item("TextXAlignment", "Center");
    e.vertical = node.item("TextYAlignment", "Center");
    e.image = node.text("Image");
    e.imageColor = node.color("ImageColor3", "FFFFFF");
    e.imageAlpha = 1 - node.number("ImageTransparency");
    return e;
}

const GuiNode* first(const std::vector<GuiNode>& nodes, const char* className) {
    for (const auto& n : nodes)
        if (n.className == className) return &n;
    return nullptr;
}

void place(const std::vector<GuiNode>& nodes, const Rect& area, const Rect& clip, int depth, const Key& key, std::vector<Placed>& elements) {
    Rect content = area;
    if (const GuiNode* padding = first(nodes, "UIPadding")) {
        auto top = padding->udim("PaddingTop"), bottom = padding->udim("PaddingBottom"), left = padding->udim("PaddingLeft"), right = padding->udim("PaddingRight");
        double l = left[0] * area.width() + left[1], r = right[0] * area.width() + right[1], t = top[0] * area.height() + top[1], b = bottom[0] * area.height() + bottom[1];
        content = Rect::make(area.minX() + l, area.minY() + t, std::max(0.0, area.width() - (l + r)), std::max(0.0, area.height() - (t + b)));
    }
    std::vector<const GuiNode*> visible;
    for (const auto& n : nodes)
        if (n.className.rfind("UI", 0) != 0 && n.className != "Folder" && n.flag("Visible", true)) visible.push_back(&n);
    std::map<std::string, Rect> frames;
    auto extent = [&](const GuiNode& node) {
        auto size = node.udim2("Size");
        return std::pair<double, double>{size[0] * content.width() + size[1], size[2] * content.height() + size[3]};
    };
    auto sorted = [&](const GuiNode& layout) {
        std::string order = layout.item("SortOrder", "Name");
        std::vector<std::pair<std::size_t, const GuiNode*>> indexed;
        for (std::size_t i = 0; i < visible.size(); ++i) indexed.emplace_back(i, visible[i]);
        std::sort(indexed.begin(), indexed.end(), [&](const auto& a, const auto& b) {
            if (order == "LayoutOrder") {
                double x = a.second->number("LayoutOrder"), y = b.second->number("LayoutOrder");
                return x != y ? x < y : a.first < b.first;
            }
            if (order == "Name") return a.second->name != b.second->name ? a.second->name < b.second->name : a.first < b.first;
            return a.first < b.first;
        });
        std::vector<const GuiNode*> out;
        for (const auto& [i, n] : indexed) out.push_back(n);
        return out;
    };
    if (const GuiNode* list = first(nodes, "UIListLayout")) {
        bool vertical = list->item("FillDirection", "Vertical") == "Vertical";
        auto padding = list->udim("Padding");
        double gap = padding[0] * (vertical ? content.height() : content.width()) + padding[1];
        auto items = sorted(*list);
        double total = 0;
        for (const auto* n : items) total += vertical ? extent(*n).second : extent(*n).first;
        total += gap * static_cast<double>(std::max<std::ptrdiff_t>(0, static_cast<std::ptrdiff_t>(items.size()) - 1));
        std::string along = vertical ? list->item("VerticalAlignment", "Top") : list->item("HorizontalAlignment", "Left");
        double span = vertical ? content.height() : content.width();
        double cursor = (vertical ? content.minY() : content.minX()) + (along == "Center" ? (span - total) / 2 : along == "Bottom" || along == "Right" ? span - total : 0);
        std::string across = vertical ? list->item("HorizontalAlignment", "Left") : list->item("VerticalAlignment", "Top");
        for (const auto* n : items) {
            auto [w, h] = extent(*n);
            double offset = vertical ? (across == "Center" ? (content.width() - w) / 2 : across == "Right" ? content.width() - w : 0)
                                     : (across == "Center" ? (content.height() - h) / 2 : across == "Bottom" ? content.height() - h : 0);
            frames[n->id] = vertical ? Rect::make(content.minX() + offset, cursor, w, h) : Rect::make(cursor, content.minY() + offset, w, h);
            cursor += (vertical ? h : w) + gap;
        }
    } else if (const GuiNode* grid = first(nodes, "UIGridLayout")) {
        auto cell = grid->udim2("CellSize"), pad = grid->udim2("CellPadding");
        double width = cell[0] * content.width() + cell[1], height = cell[2] * content.height() + cell[3];
        double gapX = pad[0] * content.width() + pad[1], gapY = pad[2] * content.height() + pad[3];
        bool horizontal = grid->item("FillDirection", "Horizontal") == "Horizontal";
        double raw = horizontal ? (content.width() + gapX) / std::max(1.0, width + gapX) : (content.height() + gapY) / std::max(1.0, height + gapY);
        long long perLine = std::max<long long>(1, static_cast<long long>(raw));
        auto items = sorted(*grid);
        for (std::size_t index = 0; index < items.size(); ++index) {
            long long major = static_cast<long long>(index) / perLine, minor = static_cast<long long>(index) % perLine;
            long long column = horizontal ? minor : major, row = horizontal ? major : minor;
            frames[items[index]->id] = Rect::make(content.minX() + static_cast<double>(column) * (width + gapX), content.minY() + static_cast<double>(row) * (height + gapY), width, height);
        }
    } else {
        for (const auto* n : visible) {
            auto [w, h] = extent(*n);
            auto position = n->udim2("Position");
            auto anchor = n->vector2("AnchorPoint");
            double x = content.minX() + (position[0] * content.width() + position[1]) - anchor[0] * w;
            double y = content.minY() + (position[2] * content.height() + position[3]) - anchor[1] * h;
            frames[n->id] = Rect::make(x, y, w, h);
        }
    }
    for (std::size_t index = 0; index < visible.size(); ++index) {
        const GuiNode& node = *visible[index];
        auto found = frames.find(node.id);
        if (found == frames.end()) continue;
        const Rect& frame = found->second;
        Key order = key;
        order.push_back(node.number("ZIndex", 1));
        order.push_back(depth);
        order.push_back(static_cast<double>(index));
        elements.push_back({order, describe(node, frame, clip)});
        Rect inner = frame;
        Rect bound = clip;
        if (node.className == "ScrollingFrame") {
            auto canvas = node.udim2("CanvasSize");
            auto scroll = node.vector2("CanvasPosition");
            inner = Rect::make(frame.minX() - scroll[0], frame.minY() - scroll[1], canvas[0] * frame.width() + canvas[1], canvas[2] * frame.height() + canvas[3]);
            bound = clip.intersection(frame);
        } else if (node.flag("ClipsDescendants", false)) {
            bound = clip.intersection(frame);
        }
        place(node.children, inner, bound, depth + 1, key, elements);
    }
}

}

std::vector<GuiElement> resolve(const std::vector<GuiNode>& screens, double width, double height) {
    std::vector<Placed> elements;
    Rect bounds = Rect::make(0, 0, width, height);
    std::vector<std::pair<std::size_t, const GuiNode*>> ordered;
    for (std::size_t i = 0; i < screens.size(); ++i) ordered.emplace_back(i, &screens[i]);
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        double x = a.second->number("DisplayOrder"), y = b.second->number("DisplayOrder");
        return x != y ? x < y : a.first < b.first;
    });
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        const GuiNode& screen = *ordered[index].second;
        if (!screen.flag("Enabled", true)) continue;
        place(screen.children, bounds, bounds, 1, {screen.number("DisplayOrder"), static_cast<double>(index)}, elements);
    }
    std::stable_sort(elements.begin(), elements.end(), [](const Placed& a, const Placed& b) { return std::lexicographical_compare(a.key.begin(), a.key.end(), b.key.begin(), b.key.end()); });
    std::vector<GuiElement> out;
    for (auto& p : elements) out.push_back(std::move(p.element));
    return out;
}

}

}
