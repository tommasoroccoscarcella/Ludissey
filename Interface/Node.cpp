#include "Node.hpp"

#include "Interface/Surface.hpp"
#include "Interface/Theme.hpp"

#include <algorithm>
#include <numeric>

namespace ui {

namespace {

double finite(double value, double fallback) { return std::isfinite(value) ? value : fallback; }

bool same(const std::optional<double>& a, const std::optional<double>& b) {
    if (a.has_value() != b.has_value()) return false;
    return !a || *a == *b || (std::isinf(*a) && std::isinf(*b));
}

class TextNode final : public Node {
public:
    std::vector<TextRun> runs;
    std::vector<bool> explicitFont, explicitColor;
    TextStyle style;
    bool explicitAlignment = false;

    void resolve(const Environment& e) override {
        for (std::size_t i = 0; i < runs.size(); ++i) {
            if (!explicitFont[i] && e.font) runs[i].font = *e.font;
            if (!explicitColor[i]) runs[i].color = e.foreground.value_or(theme::colors::textPrimary);
            if (e.disabled && !explicitColor[i]) runs[i].color = theme::colors::textDisabled;
        }
        if (!runs.empty()) {
            style.font = runs.front().font;
            style.color = runs.front().color;
        }
        if (!explicitAlignment && e.multilineAlignment) style.alignment = *e.multilineAlignment;
        cache.clear();
    }
    Size measure(Proposal p) override {
        std::optional<double> width;
        if (p.width && std::isfinite(*p.width)) width = std::max(0.0, *p.width);
        Size s = measureRuns(runs, style, width);
        return s;
    }
    void draw(Canvas& c, Surface& s) override {
        TextStyle st = style;
        c.runs(runs, frame, st);
        Node::draw(c, s);
    }
};

class SpacerNode final : public Node {
public:
    std::optional<double> minimum;
    Axis axis = Axis::vertical;
    bool inStack = false;
    bool expandsIn(Axis a) const override { return !inStack || a == axis; }
    Size measure(Proposal p) override {
        double m = minimum.value_or(8);
        if (!inStack) return {p.width.value_or(m), p.height.value_or(m)};
        if (axis == Axis::horizontal) return {std::max(m, p.width.value_or(m)), 0};
        return {0, std::max(m, p.height.value_or(m))};
    }
};

class StackNode final : public Node {
public:
    Axis axis = Axis::vertical;
    double spacing = 8;
    HAlign horizontal = HAlign::center;
    VAlign vertical = VAlign::center;

    double along(Size s) const { return axis == Axis::horizontal ? s.width : s.height; }
    double across(Size s) const { return axis == Axis::horizontal ? s.height : s.width; }
    Proposal make(std::optional<double> a, std::optional<double> c) const { return axis == Axis::horizontal ? Proposal{a, c} : Proposal{c, a}; }

    std::vector<double> distribute(std::optional<double> total, std::optional<double> cross) {
        std::vector<double> lengths(children.size(), 0);
        if (children.empty()) return lengths;
        double gaps = spacing * static_cast<double>(children.size() - 1);
        if (!total || std::isinf(*total)) {
            for (std::size_t i = 0; i < children.size(); ++i) lengths[i] = along(children[i]->size(make(total, cross)));
            return lengths;
        }
        std::vector<std::pair<double, std::size_t>> flex;
        std::vector<std::size_t> spacers;
        double reserved = 0;
        for (std::size_t i = 0; i < children.size(); ++i) {
            if (std::dynamic_pointer_cast<SpacerNode>(children[i])) {
                spacers.push_back(i);
                reserved += along(children[i]->size(make(0.0, cross)));
                continue;
            }
            double low = along(children[i]->size(make(0.0, cross)));
            double high = along(children[i]->size(make(infinity, cross)));
            flex.push_back({high - low, i});
        }
        std::stable_sort(flex.begin(), flex.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        double remaining = std::max(0.0, *total - gaps - reserved);
        std::size_t count = flex.size();
        for (const auto& [f, index] : flex) {
            double offer = remaining / static_cast<double>(count);
            double length = along(children[index]->size(make(offer, cross)));
            lengths[index] = length;
            remaining = std::max(0.0, remaining - length);
            --count;
        }
        remaining += reserved;
        count = spacers.size();
        for (std::size_t index : spacers) {
            double length = along(children[index]->size(make(remaining / static_cast<double>(count), cross)));
            lengths[index] = length;
            remaining = std::max(0.0, remaining - length);
            --count;
        }
        return lengths;
    }

    Size measure(Proposal p) override {
        if (children.empty()) return {0, 0};
        std::optional<double> total = axis == Axis::horizontal ? p.width : p.height;
        std::optional<double> cross = axis == Axis::horizontal ? p.height : p.width;
        auto lengths = distribute(total, cross);
        double sum = spacing * static_cast<double>(children.size() - 1);
        double depth = 0;
        for (std::size_t i = 0; i < children.size(); ++i) {
            sum += lengths[i];
            depth = std::max(depth, across(children[i]->size(make(lengths[i], cross))));
        }
        return axis == Axis::horizontal ? Size{sum, depth} : Size{depth, sum};
    }

    void place(Frame bounds) override {
        frame = bounds;
        double total = along({bounds.width, bounds.height});
        double cross = across({bounds.width, bounds.height});
        auto lengths = distribute(total, cross);
        double cursor = axis == Axis::horizontal ? bounds.x : bounds.y;
        for (std::size_t i = 0; i < children.size(); ++i) {
            Size s = children[i]->size(make(lengths[i], cross));
            double c = across(s);
            if (axis == Axis::horizontal) {
                double y = bounds.y + align(vertical, cross, c);
                children[i]->place({cursor, y, lengths[i], c});
            } else {
                double x = bounds.x + align(horizontal, cross, c);
                children[i]->place({x, cursor, c, lengths[i]});
            }
            cursor += lengths[i] + spacing;
        }
    }
};

class DividerNode final : public Node {
public:
    Color color;
    Axis axis = Axis::vertical;
    bool inStack = false;
    Size measure(Proposal p) override {
        if (inStack && axis == Axis::horizontal) return {1, p.height.value_or(1)};
        return {p.width.value_or(1), 1};
    }
    void draw(Canvas& c, Surface& s) override {
        c.fill(frame, color);
        Node::draw(c, s);
    }
};

class ZNode final : public Node {
public:
    Alignment alignment;
    Size measure(Proposal p) override {
        Size out;
        for (auto& c : children) {
            Size s = c->size(p);
            out.width = std::max(out.width, s.width);
            out.height = std::max(out.height, s.height);
        }
        return out;
    }
    void place(Frame bounds) override {
        frame = bounds;
        for (auto& c : children) {
            Size s = c->size({bounds.width, bounds.height});
            c->place({bounds.x + align(alignment.horizontal, bounds.width, s.width), bounds.y + align(alignment.vertical, bounds.height, s.height), s.width, s.height});
        }
    }
};

class PaddingNode final : public Node {
public:
    Insets insets;
    Size measure(Proposal p) override {
        double h = insets.leading + insets.trailing, v = insets.top + insets.bottom;
        Proposal inner{p.width ? std::optional<double>(std::max(0.0, *p.width - h)) : std::nullopt, p.height ? std::optional<double>(std::max(0.0, *p.height - v)) : std::nullopt};
        Size s = children[0]->size(inner);
        return {s.width + h, s.height + v};
    }
    void place(Frame b) override {
        frame = b;
        children[0]->place({b.x + insets.leading, b.y + insets.top, std::max(0.0, b.width - insets.leading - insets.trailing), std::max(0.0, b.height - insets.top - insets.bottom)});
    }
};

class FrameNode final : public Node {
public:
    std::optional<double> width, height, minWidth, maxWidth, minHeight, maxHeight;
    Alignment alignment;
    bool flexibleFrame = false;

    static std::optional<double> offer(std::optional<double> proposal, std::optional<double> fixedValue, std::optional<double> low, std::optional<double> high) {
        if (fixedValue) return fixedValue;
        if (!proposal) return proposal;
        double v = *proposal;
        if (low) v = std::max(v, *low);
        if (high) v = std::min(v, *high);
        return v;
    }
    static double resultFor(std::optional<double> proposal, double child, std::optional<double> fixedValue, std::optional<double> low, std::optional<double> high) {
        if (fixedValue) return *fixedValue;
        if (!low && !high) return child;
        if (high) {
            double base = proposal && std::isfinite(*proposal) ? *proposal : (proposal ? *high : child);
            if (proposal && std::isinf(*proposal)) base = *high;
            double r = std::min(*high, base);
            if (!proposal) r = std::min(*high, child);
            if (low) r = std::max(r, *low);
            return r;
        }
        return std::max(*low, child);
    }
    Size measure(Proposal p) override {
        Proposal inner{offer(p.width, width, minWidth, maxWidth), offer(p.height, height, minHeight, maxHeight)};
        Size s = children[0]->size(inner);
        return {resultFor(p.width, s.width, width, minWidth, maxWidth), resultFor(p.height, s.height, height, minHeight, maxHeight)};
    }
    void place(Frame b) override {
        frame = b;
        Size s = children[0]->size({b.width, b.height});
        s.width = std::min(s.width, finite(s.width, b.width));
        s.height = std::min(s.height, finite(s.height, b.height));
        children[0]->place({b.x + align(alignment.horizontal, b.width, s.width), b.y + align(alignment.vertical, b.height, s.height), s.width, s.height});
    }
};

class FixedNode final : public Node {
public:
    bool horizontal = true, vertical = true;
    Size measure(Proposal p) override { return children[0]->size({horizontal ? std::nullopt : p.width, vertical ? std::nullopt : p.height}); }
    void place(Frame b) override {
        frame = b;
        Size s = children[0]->size({horizontal ? std::nullopt : std::optional<double>(b.width), vertical ? std::nullopt : std::optional<double>(b.height)});
        children[0]->place({b.x + (b.width - s.width) / 2, b.y + (b.height - s.height) / 2, s.width, s.height});
    }
};

class DecorationNode final : public Node {
public:
    NodePtr decoration;
    bool behind = true;
    Alignment alignment;
    void resolve(const Environment& e) override {
        Node::resolve(e);
        decoration->resolve(e);
    }
    Size measure(Proposal p) override { return children[0]->size(p); }
    void place(Frame b) override {
        frame = b;
        children[0]->place(b);
        Size s = decoration->size({b.width, b.height});
        s.width = finite(s.width, b.width);
        s.height = finite(s.height, b.height);
        decoration->place({b.x + align(alignment.horizontal, b.width, s.width), b.y + align(alignment.vertical, b.height, s.height), s.width, s.height});
    }
    void draw(Canvas& c, Surface& s) override {
        if (behind) decoration->draw(c, s);
        children[0]->draw(c, s);
        if (!behind) decoration->draw(c, s);
    }
};

bool plain(const Node& node);

class EffectNode final : public Node {
public:
    double alpha = 1;
    bool clip = false;
    double clipRadius = 0;
    Point shift;
    std::optional<Color> shadowColor;
    Point shadowOffset;
    double shadowBlur = 0;
    std::optional<double> shadowRadius;
    bool invisible = false;
    Size measure(Proposal p) override { return children[0]->size(p); }
    void place(Frame b) override {
        frame = b;
        children[0]->place({b.x + shift.x, b.y + shift.y, b.width, b.height});
    }
    void draw(Canvas& c, Surface& s) override {
        if (invisible) return;
        double reach = shadowColor ? shadowBlur * 2 + std::abs(shadowOffset.x) + std::abs(shadowOffset.y) + 2 : 0;
        Frame bounds{frame.x + std::min(0.0, shift.x) - reach, frame.y + std::min(0.0, shift.y) - reach, frame.width + std::abs(shift.x) + reach * 2, frame.height + std::abs(shift.y) + reach * 2};
        bool grouped = shadowColor && !shadowRadius && (shadowBlur > 0 || !plain(*children[0]));
        bool layered = alpha < 1 || grouped;
        if (layered && !c.intersectsClip(bounds)) return;
        c.save();
        if (alpha < 1) {
            c.setAlpha(alpha);
            c.beginLayer(bounds);
        }
        if (clip) {
            c.clip(frame, clipRadius);
            s.pushClip(frame);
        }
        if (shadowColor && shadowRadius) c.boxShadow(frame, *shadowRadius, shadowOffset.x, shadowOffset.y, shadowBlur, *shadowColor);
        else if (shadowColor) {
            c.shadow(shadowOffset.x, shadowOffset.y, shadowBlur, *shadowColor);
            if (grouped) c.beginLayer(bounds);
        }
        children[0]->draw(c, s);
        if (grouped) c.endLayer();
        if (clip) s.popClip();
        if (alpha < 1) c.endLayer();
        c.restore();
    }
};

void disableTree(const NodePtr& n) {
    n->interaction.enabled = false;
    for (auto& c : n->children) disableTree(c);
}

class EnvironmentNode final : public Node {
public:
    std::optional<Font> fontValue;
    std::optional<Color> color;
    std::optional<bool> disabledValue;
    std::optional<bool> compactValue;
    std::optional<TextAlign> textAlignment;
    void resolve(const Environment& e) override {
        Environment inner = e;
        if (compactValue) inner.compact = *compactValue;
        if (fontValue) inner.font = fontValue;
        if (color) inner.foreground = color;
        if (disabledValue) inner.disabled = inner.disabled || *disabledValue;
        if (textAlignment) inner.multilineAlignment = textAlignment;
        for (auto& c : children) c->resolve(inner);
        if (disabledValue && *disabledValue) disableTree(children[0]);
        cache.clear();
    }
    Size measure(Proposal p) override { return children[0]->size(p); }
};

class ShapeNode final : public Node {
public:
    enum class Kind { rectangle, ellipse } kind = Kind::rectangle;
    Color fillColor = Color::clear();
    Color strokeColor = Color::clear();
    double strokeWidth = 0;
    double radius = 0;
    std::optional<Gradient> gradient;
    Size measure(Proposal p) override { return {p.width.value_or(10), p.height.value_or(10)}; }
    bool expandsIn(Axis) const override { return true; }
    void draw(Canvas& c, Surface& s) override {
        if (kind == Kind::ellipse) {
            c.fillEllipse(frame, fillColor);
            if (strokeWidth > 0) c.strokeEllipse(frame, strokeColor, strokeWidth);
        } else {
            if (gradient) c.fillGradient(frame, *gradient, radius);
            c.fill(frame, fillColor, radius);
            if (strokeWidth > 0) c.stroke(frame, strokeColor, strokeWidth, radius);
        }
        Node::draw(c, s);
    }
};

class ImageNode final : public Node {
public:
    Image picture;
    double width = 0, height = 0;
    std::optional<Color> tint;
    double alpha = 1;
    bool templated = false;
    void resolve(const Environment& e) override {
        if (templated) tint = e.disabled ? theme::colors::textDisabled : e.foreground.value_or(theme::colors::textPrimary);
    }
    Size measure(Proposal) override { return {width, height}; }
    void draw(Canvas& c, Surface& s) override {
        c.image(picture, frame, tint, alpha);
        Node::draw(c, s);
    }
};

class PictureNode final : public Node {
public:
    Image source;
    bool fill = false;
    double alpha = 1;
    Size measure(Proposal p) override {
        double w = source.pixelsWide / 2.0, h = source.pixelsHigh / 2.0;
        if (!p.width && !p.height) return {w, h};
        double pw = p.width && std::isfinite(*p.width) ? *p.width : 0, ph = p.height && std::isfinite(*p.height) ? *p.height : 0;
        if (p.width && p.height && std::isfinite(*p.width) && std::isfinite(*p.height)) {
            if (fill) return {pw, ph};
            double s = std::min(pw / std::max(1.0, w), ph / std::max(1.0, h));
            return {w * s, h * s};
        }
        if (pw > 0) return {pw, h * pw / std::max(1.0, w)};
        if (ph > 0) return {w * ph / std::max(1.0, h), ph};
        return {w, h};
    }
    bool expandsIn(Axis) const override { return true; }
    void draw(Canvas& c, Surface& s) override {
        if (!source.valid() || frame.empty()) return;
        double w = source.pixelsWide, h = source.pixelsHigh;
        double scale = fill ? std::max(frame.width / w, frame.height / h) : std::min(frame.width / w, frame.height / h);
        Frame target{frame.midX() - w * scale / 2, frame.midY() - h * scale / 2, w * scale, h * scale};
        c.save();
        if (fill) c.clip(frame);
        c.image(source, target, std::nullopt, alpha);
        c.restore();
        Node::draw(c, s);
    }
};

class ReaderNode final : public Node {
public:
    std::function<NodePtr(const Environment&)> build;
    void resolve(const Environment& e) override {
        NodePtr built = build(e);
        children = {built ? built : empty()};
        children[0]->resolve(e);
        cache.clear();
    }
    Size measure(Proposal p) override { return children.empty() ? Size{} : children[0]->size(p); }
};

class ScrollNode final : public Node {
public:
    std::string key;
    Axis axis = Axis::vertical;
    bool indicators = true;
    Size content;
    Size measure(Proposal p) override {
        Proposal inner = axis == Axis::vertical ? Proposal{p.width, std::nullopt} : Proposal{std::nullopt, p.height};
        Size s = children[0]->size(inner);
        return {p.width ? finite(*p.width, s.width) : s.width, p.height ? finite(*p.height, s.height) : s.height};
    }
    bool expandsIn(Axis) const override { return true; }
    void place(Frame b) override {
        frame = b;
        Proposal inner = axis == Axis::vertical ? Proposal{b.width, std::nullopt} : Proposal{std::nullopt, b.height};
        content = children[0]->size(inner);
        if (axis == Axis::vertical) content.width = b.width;
        else content.height = b.height;
        Surface* surface = Surface::active();
        Point offset = surface ? surface->scrollOffset(key) : Point{};
        double maxX = std::max(0.0, content.width - b.width), maxY = std::max(0.0, content.height - b.height);
        offset.x = std::clamp(offset.x, 0.0, maxX);
        offset.y = std::clamp(offset.y, 0.0, maxY);
        if (surface) surface->setScrollOffset(key, offset, {b.width, b.height}, content);
        children[0]->place({b.x - offset.x, b.y - offset.y, content.width, content.height});
    }
    void draw(Canvas& c, Surface& s) override {
        c.save();
        c.clip(frame);
        s.pushClip(frame);
        s.registerScroll(key, frame);
        children[0]->draw(c, s);
        s.popClip();
        c.restore();
        if (indicators && axis == Axis::vertical && content.height > frame.height + 0.5 && s.scrollActive(key)) {
            Point offset = s.scrollOffset(key);
            double ratio = frame.height / content.height;
            double knob = std::max(24.0, frame.height * ratio);
            double y = frame.y + (frame.height - knob) * (offset.y / std::max(1.0, content.height - frame.height));
            c.fill({frame.maxX() - theme::spacing::s3, y + theme::spacing::s1, theme::spacing::s2, knob - theme::spacing::s2}, theme::colors::borderStrong, theme::spacing::s1);
        }
    }
};

class GridNode final : public Node {
public:
    double minimum = 100, maximum = infinity, spacing = 12, rowSpacing = 12;
    int fixedColumns = 0;
    int count(double width) const {
        if (fixedColumns > 0) return fixedColumns;
        return std::max(1, static_cast<int>(std::floor((width + spacing) / (minimum + spacing))));
    }
    double cell(double width) const {
        int n = count(width);
        double w = (width - spacing * (n - 1)) / n;
        return std::min(w, maximum);
    }
    Size measure(Proposal p) override {
        double width = p.width && std::isfinite(*p.width) ? *p.width : minimum * std::max<std::size_t>(1, children.size()) + spacing * static_cast<double>(children.size() ? children.size() - 1 : 0);
        int n = count(width);
        double w = cell(width);
        double height = 0;
        for (std::size_t row = 0; row * static_cast<std::size_t>(n) < children.size(); ++row) {
            double h = 0;
            for (std::size_t i = row * static_cast<std::size_t>(n); i < std::min(children.size(), (row + 1) * static_cast<std::size_t>(n)); ++i) h = std::max(h, children[i]->size({w, std::nullopt}).height);
            height += h + (row ? rowSpacing : 0);
        }
        return {width, height};
    }
    void place(Frame b) override {
        frame = b;
        int n = count(b.width);
        double w = cell(b.width);
        double y = b.y;
        for (std::size_t row = 0; row * static_cast<std::size_t>(n) < children.size(); ++row) {
            double h = 0;
            std::size_t first = row * static_cast<std::size_t>(n), last = std::min(children.size(), (row + 1) * static_cast<std::size_t>(n));
            for (std::size_t i = first; i < last; ++i) h = std::max(h, children[i]->size({w, std::nullopt}).height);
            double total = w * n + spacing * (n - 1);
            double x = b.x + (std::isfinite(maximum) ? 0 : (b.width - total) / 2);
            for (std::size_t i = first; i < last; ++i) {
                Size s = children[i]->size({w, std::nullopt});
                children[i]->place({x + static_cast<double>(i - first) * (w + spacing), y + (h - s.height) / 2, w, s.height});
            }
            y += h + rowSpacing;
        }
    }
};

class GeometryNode final : public Node {
public:
    std::function<NodePtr(Size)> build;
    Environment environment;
    void resolve(const Environment& e) override { environment = e; }
    Size measure(Proposal p) override { return {p.width.value_or(10), p.height.value_or(10)}; }
    bool expandsIn(Axis) const override { return true; }
    void place(Frame b) override {
        frame = b;
        children = {build({b.width, b.height})};
        children[0]->resolve(environment);
        if (environment.disabled) disableTree(children[0]);
        Size s = children[0]->size({b.width, b.height});
        children[0]->place({b.x, b.y, std::min(s.width, b.width), std::min(s.height, b.height)});
    }
};

class CanvasNode final : public Node {
public:
    std::function<void(Canvas&, const Frame&)> painter;
    std::optional<Size> ideal;
    Size measure(Proposal p) override {
        Size i = ideal.value_or(Size{10, 10});
        return {p.width && std::isfinite(*p.width) ? *p.width : i.width, p.height && std::isfinite(*p.height) ? *p.height : i.height};
    }
    void draw(Canvas& c, Surface& s) override {
        c.save();
        painter(c, frame);
        c.restore();
        Node::draw(c, s);
    }
};

class FieldNode final : public Node {
public:
    NativeField field;
    Environment environment;
    void resolve(const Environment& e) override { environment = e; }
    Size measure(Proposal p) override {
        if (!field.options.empty()) {
            double widest = 0;
            for (const auto& option : field.options) widest = std::max(widest, measureText(option, {Font::system(13), Color::black()}, std::nullopt).width);
            return {p.width && std::isfinite(*p.width) ? *p.width : widest + 36, 21};
        }
        return {p.width && std::isfinite(*p.width) ? *p.width : 120, field.multiline ? p.height.value_or(60) : lineHeight(field.font)};
    }
    bool expandsIn(Axis a) const override { return a == Axis::horizontal || field.multiline; }
    void draw(Canvas& c, Surface& s) override {
        NativeField f = field;
        f.full = frame;
        f.frame = s.visible(frame);
        f.clip = s.clipFrame();
        if (environment.disabled) f.editable = false;
        s.registerField(f);
        Node::draw(c, s);
    }
};

class ViewportNode final : public Node {
public:
    std::string key;
    Size measure(Proposal p) override { return {p.width.value_or(100), p.height.value_or(100)}; }
    bool expandsIn(Axis) const override { return true; }
    void draw(Canvas& c, Surface& s) override {
        c.erase(frame);
        s.registerViewport({key, s.visible(frame)});
        Node::draw(c, s);
    }
};

template <typename T>
std::shared_ptr<T> wrap(NodePtr child) {
    auto n = std::make_shared<T>();
    n->children.push_back(std::move(child));
    return n;
}

void markStack(const std::shared_ptr<StackNode>& stack) {
    for (auto& c : stack->children) {
        if (auto s = std::dynamic_pointer_cast<SpacerNode>(c)) {
            s->inStack = true;
            s->axis = stack->axis;
        }
        if (auto d = std::dynamic_pointer_cast<DividerNode>(c)) {
            d->inStack = true;
            d->axis = stack->axis;
        }
    }
}

}

Size Node::measure(Proposal p) {
    if (children.empty()) return {0, 0};
    return children[0]->size(p);
}

void Node::place(Frame bounds) {
    frame = bounds;
    for (auto& c : children) c->place(bounds);
}

void Node::drawChildren(Canvas& canvas, Surface& surface) {
    for (auto& c : children) c->draw(canvas, surface);
}

void Node::draw(Canvas& canvas, Surface& surface) {
    if (!accessibility.empty() || !label.empty()) surface.registerElement(*this);
    if (interactive) surface.registerInteraction(interaction, frame);
    drawChildren(canvas, surface);
}

void Node::resolve(const Environment& environment) {
    for (auto& c : children) c->resolve(environment);
    cache.clear();
}

Size Node::size(Proposal proposal) {
    for (const auto& c : cache)
        if (same(c.width, proposal.width) && same(c.height, proposal.height)) return c.size;
    Size s = measure(proposal);
    if (cache.size() > 24) cache.erase(cache.begin());
    cache.push_back({proposal.width, proposal.height, s});
    return s;
}

NodePtr Node::padding(double all) { return padding(Insets::all(all)); }

NodePtr Node::padding(Insets insets) {
    auto n = wrap<PaddingNode>(shared_from_this());
    n->insets = insets;
    return n;
}

NodePtr Node::paddingHorizontal(double value) { return padding(Insets::horizontal(value)); }
NodePtr Node::paddingVertical(double value) { return padding(Insets::vertical(value)); }

NodePtr Node::frameSize(std::optional<double> width, std::optional<double> height, Alignment alignment) {
    auto n = wrap<FrameNode>(shared_from_this());
    n->width = width;
    n->height = height;
    n->alignment = alignment;
    return n;
}

NodePtr Node::flexible(std::optional<double> minWidth, std::optional<double> maxWidth, std::optional<double> minHeight, std::optional<double> maxHeight, Alignment alignment) {
    auto n = wrap<FrameNode>(shared_from_this());
    n->minWidth = minWidth;
    n->maxWidth = maxWidth;
    n->minHeight = minHeight;
    n->maxHeight = maxHeight;
    n->alignment = alignment;
    n->flexibleFrame = true;
    return n;
}

NodePtr Node::fillWidth(Alignment alignment) { return flexible(std::nullopt, infinity, std::nullopt, std::nullopt, alignment); }
NodePtr Node::fill(Alignment alignment) { return flexible(std::nullopt, infinity, std::nullopt, infinity, alignment); }

NodePtr Node::fixed(bool horizontal, bool vertical) {
    auto n = wrap<FixedNode>(shared_from_this());
    n->horizontal = horizontal;
    n->vertical = vertical;
    return n;
}

NodePtr Node::background(NodePtr node, Alignment alignment) {
    auto n = wrap<DecorationNode>(shared_from_this());
    n->decoration = std::move(node);
    n->behind = true;
    n->alignment = alignment;
    return n;
}

NodePtr Node::background(Color color, double radius) { return background(roundedRectangle(radius, color)); }

NodePtr Node::overlay(NodePtr node, Alignment alignment) {
    auto n = wrap<DecorationNode>(shared_from_this());
    n->decoration = std::move(node);
    n->behind = false;
    n->alignment = alignment;
    return n;
}

NodePtr Node::border(Color color, double width, double radius) { return overlay(strokeRectangle(color, width, radius)); }

NodePtr Node::clipped(double radius) {
    auto n = wrap<EffectNode>(shared_from_this());
    n->clip = true;
    n->clipRadius = radius;
    return n;
}

NodePtr Node::opacity(double value) {
    auto n = wrap<EffectNode>(shared_from_this());
    n->alpha = value;
    return n;
}

NodePtr Node::offset(double dx, double dy) {
    auto n = wrap<EffectNode>(shared_from_this());
    n->shift = {dx, dy};
    return n;
}

namespace {

bool plain(const Node& node) {
    if (dynamic_cast<const DecorationNode*>(&node) || dynamic_cast<const ShapeNode*>(&node) || dynamic_cast<const PictureNode*>(&node)) return false;
    for (const auto& child : node.children)
        if (child && !plain(*child)) return false;
    return true;
}

}

NodePtr Node::shadow(Color color, double dx, double dy, double blur) {
    auto n = wrap<EffectNode>(shared_from_this());
    n->shadowColor = color;
    n->shadowOffset = {dx, dy};
    n->shadowBlur = blur;
    return n;
}

NodePtr Node::boxShadow(Color color, double dx, double dy, double blur, double radius) {
    auto n = wrap<EffectNode>(shared_from_this());
    n->shadowColor = color;
    n->shadowOffset = {dx, dy};
    n->shadowBlur = blur;
    n->shadowRadius = radius;
    return n;
}

NodePtr Node::hidden(bool value) {
    auto n = wrap<EffectNode>(shared_from_this());
    n->invisible = value;
    return n;
}

NodePtr Node::font(Font value) {
    auto n = wrap<EnvironmentNode>(shared_from_this());
    n->fontValue = value;
    return n;
}

NodePtr Node::foreground(Color color) {
    auto n = wrap<EnvironmentNode>(shared_from_this());
    n->color = color;
    return n;
}

NodePtr Node::disabled(bool value) {
    auto n = wrap<EnvironmentNode>(shared_from_this());
    n->disabledValue = value;
    return n;
}

NodePtr Node::compact(bool value) {
    auto n = wrap<EnvironmentNode>(shared_from_this());
    n->compactValue = value;
    return n;
}

NodePtr Node::multilineAlignment(TextAlign alignment) {
    auto n = wrap<EnvironmentNode>(shared_from_this());
    n->textAlignment = alignment;
    return n;
}

NodePtr Node::onTap(std::string identifier, std::function<void()> action, Cursor cursor) {
    auto n = wrap<Node>(shared_from_this());
    n->interactive = true;
    n->interaction.id = identifier;
    n->interaction.tap = std::move(action);
    n->interaction.cursor = cursor;
    n->interaction.tracksHover = true;
    if (!identifier.empty()) n->accessibility = identifier;
    return n;
}

NodePtr Node::onHover(std::string identifier, std::function<void(bool)> hover) {
    auto n = wrap<Node>(shared_from_this());
    n->interactive = true;
    n->interaction.id = std::move(identifier);
    n->interaction.hover = std::move(hover);
    n->interaction.tracksHover = true;
    return n;
}

NodePtr Node::onDrag(std::string identifier, std::function<void(const DragEvent&)> drag, Cursor cursor) {
    auto n = wrap<Node>(shared_from_this());
    n->interactive = true;
    n->interaction.id = std::move(identifier);
    n->interaction.drag = std::move(drag);
    n->interaction.cursor = cursor;
    n->interaction.tracksHover = true;
    return n;
}

NodePtr Node::contextMenu(std::function<std::vector<MenuItem>()> items) {
    auto n = wrap<Node>(shared_from_this());
    n->interactive = true;
    n->interaction.menu = std::move(items);
    return n;
}

NodePtr Node::pullDownMenu(std::string identifier, std::function<std::vector<MenuItem>()> items) {
    auto n = wrap<Node>(shared_from_this());
    n->interactive = true;
    n->interaction.id = std::move(identifier);
    n->interaction.menu = std::move(items);
    n->interaction.pullDown = true;
    n->interaction.tracksHover = true;
    n->interaction.cursor = Cursor::pointer;
    return n;
}

NodePtr Node::help(std::string value) {
    auto n = wrap<Node>(shared_from_this());
    n->interactive = true;
    n->interaction.help = std::move(value);
    return n;
}

NodePtr Node::id(std::string identifier) {
    auto n = wrap<Node>(shared_from_this());
    n->accessibility = std::move(identifier);
    return n;
}

NodePtr Node::named(std::string text) {
    auto n = wrap<Node>(shared_from_this());
    n->label = std::move(text);
    return n;
}

NodePtr text(std::string value, std::optional<Font> font, std::optional<Color> color) {
    auto n = std::make_shared<TextNode>();
    n->runs = {TextRun{std::move(value), font.value_or(theme::type::body), color.value_or(theme::colors::textPrimary)}};
    n->explicitFont = {font.has_value()};
    n->explicitColor = {color.has_value()};
    n->style.font = n->runs[0].font;
    n->style.color = n->runs[0].color;
    n->label = n->runs[0].text;
    return n;
}

NodePtr styledText(std::vector<TextRun> runs) {
    auto n = std::make_shared<TextNode>();
    n->runs = std::move(runs);
    n->explicitFont.assign(n->runs.size(), true);
    n->explicitColor.assign(n->runs.size(), true);
    if (!n->runs.empty()) {
        n->style.font = n->runs[0].font;
        n->style.color = n->runs[0].color;
    }
    for (const auto& r : n->runs) n->label += r.text;
    return n;
}

NodePtr lineLimited(NodePtr node, int lines, Truncation truncation) {
    if (auto t = std::dynamic_pointer_cast<TextNode>(node)) {
        t->style.lineLimit = lines;
        t->style.truncation = truncation;
    }
    return node;
}

NodePtr scaledText(NodePtr node, double minimum) {
    if (auto t = std::dynamic_pointer_cast<TextNode>(node)) t->style.minimumScale = minimum;
    return node;
}

NodePtr hstack(std::vector<NodePtr> children, double spacing, VAlign alignment) {
    auto n = std::make_shared<StackNode>();
    n->axis = Axis::horizontal;
    n->spacing = spacing;
    n->vertical = alignment;
    for (auto& c : children)
        if (c) n->children.push_back(std::move(c));
    markStack(n);
    return n;
}

NodePtr vstack(std::vector<NodePtr> children, double spacing, HAlign alignment) {
    auto n = std::make_shared<StackNode>();
    n->axis = Axis::vertical;
    n->spacing = spacing;
    n->horizontal = alignment;
    for (auto& c : children)
        if (c) n->children.push_back(std::move(c));
    markStack(n);
    return n;
}

NodePtr zstack(std::vector<NodePtr> children, Alignment alignment) {
    auto n = std::make_shared<ZNode>();
    n->alignment = alignment;
    for (auto& c : children)
        if (c) n->children.push_back(std::move(c));
    return n;
}

NodePtr spacer(std::optional<double> minimum) {
    auto n = std::make_shared<SpacerNode>();
    n->minimum = minimum;
    return n;
}

NodePtr divider(Color color) {
    auto n = std::make_shared<DividerNode>();
    n->color = color;
    return n;
}

NodePtr rectangle(Color fill) {
    auto n = std::make_shared<ShapeNode>();
    n->fillColor = fill;
    return n;
}

NodePtr roundedRectangle(double radius, Color fill) {
    auto n = std::make_shared<ShapeNode>();
    n->fillColor = fill;
    n->radius = radius;
    return n;
}

NodePtr strokeRectangle(Color stroke, double width, double radius) {
    auto n = std::make_shared<ShapeNode>();
    n->strokeColor = stroke;
    n->strokeWidth = width;
    n->radius = radius;
    return n;
}

NodePtr circle(Color fill) {
    auto n = std::make_shared<ShapeNode>();
    n->kind = ShapeNode::Kind::ellipse;
    n->fillColor = fill;
    return n;
}

NodePtr ringShape(Color stroke, double width) {
    auto n = std::make_shared<ShapeNode>();
    n->kind = ShapeNode::Kind::ellipse;
    n->strokeColor = stroke;
    n->strokeWidth = width;
    return n;
}

NodePtr gradient(Gradient value, double radius) {
    auto n = std::make_shared<ShapeNode>();
    n->gradient = value;
    n->radius = radius;
    return n;
}

NodePtr empty() { return std::make_shared<Node>(); }

NodePtr image(Image picture, double width, double height, std::optional<Color> tint, double opacity) {
    auto n = std::make_shared<ImageNode>();
    n->picture = std::move(picture);
    n->width = width;
    n->height = height;
    n->tint = tint;
    n->alpha = opacity;
    return n;
}

NodePtr templateImage(Image picture, double width, double height) {
    auto n = std::make_shared<ImageNode>();
    n->picture = std::move(picture);
    n->width = width;
    n->height = height;
    n->templated = true;
    return n;
}

NodePtr picture(Image image, bool fill, double opacity) {
    auto n = std::make_shared<PictureNode>();
    n->source = std::move(image);
    n->fill = fill;
    n->alpha = opacity;
    return n;
}

NodePtr reader(std::function<NodePtr(const Environment&)> build) {
    auto n = std::make_shared<ReaderNode>();
    n->build = std::move(build);
    return n;
}

NodePtr scroll(std::string key, NodePtr content, Axis axis, bool indicators) {
    auto n = wrap<ScrollNode>(std::move(content));
    n->key = std::move(key);
    n->axis = axis;
    n->indicators = indicators;
    return n;
}

NodePtr grid(std::vector<NodePtr> items, double minimum, double maximum, double spacing, double rowSpacing) {
    auto n = std::make_shared<GridNode>();
    n->children = std::move(items);
    n->minimum = minimum;
    n->maximum = maximum;
    n->spacing = spacing;
    n->rowSpacing = rowSpacing;
    return n;
}

NodePtr columns(std::vector<NodePtr> items, int count, double spacing, double rowSpacing) {
    auto n = std::make_shared<GridNode>();
    n->children = std::move(items);
    n->fixedColumns = count;
    n->spacing = spacing;
    n->rowSpacing = rowSpacing;
    return n;
}

NodePtr geometry(std::function<NodePtr(Size)> build) {
    auto n = std::make_shared<GeometryNode>();
    n->build = std::move(build);
    return n;
}

NodePtr canvas(std::function<void(Canvas&, const Frame&)> draw, std::optional<Size> ideal) {
    auto n = std::make_shared<CanvasNode>();
    n->painter = std::move(draw);
    n->ideal = ideal;
    return n;
}

NodePtr textField(NativeField field) {
    auto n = std::make_shared<FieldNode>();
    n->accessibility = field.id;
    n->field = std::move(field);
    return n;
}

NodePtr popup(NativeField field) {
    auto n = std::make_shared<FieldNode>();
    n->accessibility = field.id;
    n->field = std::move(field);
    return n;
}

NodePtr viewport(std::string key) {
    auto n = std::make_shared<ViewportNode>();
    n->key = std::move(key);
    n->accessibility = n->key;
    return n;
}

NodePtr when(bool condition, NodePtr node) { return condition ? std::move(node) : nullptr; }

}
