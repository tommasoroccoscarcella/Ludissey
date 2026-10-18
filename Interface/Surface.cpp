#include "Surface.hpp"

#include <chrono>

namespace ui {

namespace {

thread_local Surface* current = nullptr;

double seconds() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

class Scope {
public:
    explicit Scope(Surface* s) : previous(current) { current = s; }
    ~Scope() { current = previous; }

private:
    Surface* previous;
};

}

Surface::Surface(std::function<NodePtr()> make) : build(std::move(make)) {}

Surface* Surface::active() { return current; }

void Surface::resize(Size size, double scale) {
    if (size == bounds && scale == factor) return;
    bounds = size;
    factor = scale;
    dirty = true;
}

void Surface::layout() {
    Scope scope(this);
    tree = build();
    if (!tree) tree = empty();
    tree->resolve(environment);
    tree->place({0, 0, bounds.width, bounds.height});
}

void Surface::collect() {
    regions.clear();
    clips.clear();
    nativeFields.clear();
    slots.clear();
    registered.clear();
    scrollRegions.clear();
}

void Surface::render(Canvas& canvas) {
    Scope scope(this);
    animating = false;
    layout();
    collect();
    canvas.clear(Color::clear());
    tree->draw(canvas, *this);
    dirty = false;
    double now = seconds();
    for (auto& [key, state] : scrolls) {
        if (now - state.activity < 1.2) animating = true;
    }
    if (lastMouse.x >= 0) currentCursor = cursorAt(lastMouse);
}

void Surface::pushClip(const Frame& frame) { clips.push_back(clips.empty() ? frame : clips.back().intersection(frame)); }

void Surface::popClip() {
    if (!clips.empty()) clips.pop_back();
}

Frame Surface::visible(const Frame& frame) const { return clips.empty() ? frame : clips.back().intersection(frame); }

Frame Surface::clipFrame() const { return clips.empty() ? Frame{0, 0, bounds.width, bounds.height} : clips.back().intersection({0, 0, bounds.width, bounds.height}); }

void Surface::registerInteraction(const Interaction& interaction, const Frame& frame) {
    Frame v = visible(frame);
    if (v.empty()) return;
    regions.push_back({interaction, v});
}

void Surface::registerElement(const Node& node) {
    registered.push_back({node.accessibility, node.label, visible(node.frame), node.interaction.enabled});
}

void Surface::registerField(const NativeField& field) {
    if (field.frame.empty()) return;
    nativeFields.push_back(field);
    registered.push_back({field.id, field.text, field.frame, field.editable});
}

void Surface::registerViewport(const ViewportSlot& slot) { slots.push_back(slot); }

void Surface::registerScroll(const std::string& key, const Frame& frame) {
    scrollRegions.push_back({key, visible(frame)});
    scrolls[key].frame = frame;
}

Point Surface::scrollOffset(const std::string& key) const {
    auto it = scrolls.find(key);
    return it == scrolls.end() ? Point{} : it->second.offset;
}

void Surface::setScrollOffset(const std::string& key, Point offset, Size viewport, Size content) {
    auto& s = scrolls[key];
    s.offset = offset;
    s.viewport = viewport;
    s.content = content;
}

void Surface::scrollTo(const std::string& key, Point offset) {
    scrolls[key].offset = offset;
    dirty = true;
}

bool Surface::scrollActive(const std::string& key) const {
    auto it = scrolls.find(key);
    return it != scrolls.end() && seconds() - it->second.activity < 1.2;
}

bool Surface::covers(Point location) const {
    for (const auto& r : regions)
        if (r.frame.contains(location) && (r.interaction.tap || r.interaction.tapAt || r.interaction.drag || r.interaction.press || r.interaction.doubleTap || r.interaction.hover || r.interaction.secondaryTap || r.interaction.menu)) return true;
    return false;
}

std::optional<std::vector<MenuItem>> Surface::menuAt(Point location) const {
    for (auto it = regions.rbegin(); it != regions.rend(); ++it)
        if (it->interaction.menu && it->interaction.enabled && it->frame.contains(location)) return it->interaction.menu();
    return std::nullopt;
}

std::optional<std::pair<std::vector<MenuItem>, Frame>> Surface::pullDownAt(Point location) const {
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!it->frame.contains(location)) continue;
        if (it->interaction.pullDown && it->interaction.menu && it->interaction.enabled) return std::make_pair(it->interaction.menu(), it->frame);
        if (it->interaction.tap || it->interaction.tapAt || it->interaction.drag || it->interaction.press || it->interaction.doubleTap) return std::nullopt;
    }
    return std::nullopt;
}

std::optional<std::vector<MenuItem>> Surface::menu(const std::string& id) const {
    for (auto it = regions.rbegin(); it != regions.rend(); ++it)
        if (it->interaction.id == id && it->interaction.menu && it->interaction.enabled) return it->interaction.menu();
    return std::nullopt;
}

std::string Surface::tooltip() const {
    if (lastMouse.x < 0) return "";
    for (auto it = regions.rbegin(); it != regions.rend(); ++it)
        if (!it->interaction.help.empty() && it->frame.contains(lastMouse)) return it->interaction.help;
    return "";
}

const Surface::Region* Surface::hit(Point location, bool needsTap) const {
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!it->frame.contains(location)) continue;
        if (!it->interaction.enabled) return needsTap ? nullptr : &*it;
        bool actionable = it->interaction.tap || it->interaction.tapAt || it->interaction.drag || it->interaction.press || it->interaction.doubleTap;
        if (actionable || (!needsTap && it->interaction.tracksHover)) return &*it;
    }
    return nullptr;
}

namespace {

bool actionable(const Interaction& i) { return i.tap || i.tapAt || i.drag || i.press || i.doubleTap || i.menu; }

}

Cursor Surface::cursorAt(Point location) const {
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!it->frame.contains(location)) continue;
        if (!it->interaction.enabled) return Cursor::arrow;
        if (actionable(it->interaction)) return it->interaction.cursor;
    }
    return Cursor::arrow;
}

std::vector<Surface::RegionInfo> Surface::regionInfo() const {
    std::vector<RegionInfo> out;
    for (const auto& r : regions) out.push_back({r.interaction.id, r.frame, r.interaction.enabled, actionable(r.interaction), r.interaction.tracksHover, static_cast<bool>(r.interaction.hover), r.interaction.cursor});
    return out;
}

void Surface::updateHover(Point location) {
    std::set<std::string> next;
    for (const auto& r : regions)
        if (r.interaction.tracksHover && r.interaction.enabled && r.frame.contains(location) && !r.interaction.id.empty()) next.insert(r.interaction.id);
    if (next == hover) return;
    for (const auto& r : regions) {
        if (!r.interaction.hover || r.interaction.id.empty()) continue;
        bool was = hover.count(r.interaction.id) > 0, now = next.count(r.interaction.id) > 0;
        if (was != now) r.interaction.hover(now);
    }
    hover = std::move(next);
    dirty = true;
}

void Surface::mouseMoved(Point location) {
    lastMouse = location;
    updateHover(location);
    Cursor next = cursorAt(location);
    if (next != currentCursor) {
        currentCursor = next;
        dirty = true;
    }
}

void Surface::mouseExited() {
    lastMouse = {-1, -1};
    if (!hover.empty()) {
        for (const auto& r : regions)
            if (r.interaction.hover && hover.count(r.interaction.id)) r.interaction.hover(false);
        hover.clear();
        dirty = true;
    }
    currentCursor = Cursor::arrow;
}

void Surface::mouseDown(Point location, int clicks) {
    lastMouse = location;
    const Region* r = hit(location, true);
    dragging.reset();
    dragMoved = false;
    if (!r) return;
    dragging = *r;
    dragStart = location;
    dragLast = location;
    pressedId = r->interaction.id;
    if (r->interaction.press) r->interaction.press({location.x - r->frame.x, location.y - r->frame.y});
    if (clicks == 2 && r->interaction.doubleTap) {
        r->interaction.doubleTap();
        dragging->interaction.tap = nullptr;
        dragging->interaction.tapAt = nullptr;
    }
    dirty = true;
}

void Surface::mouseDragged(Point location) {
    lastMouse = location;
    if (!dragging) return;
    if (!dragMoved && std::hypot(location.x - dragStart.x, location.y - dragStart.y) < 3) return;
    dragMoved = true;
    if (dragging->interaction.drag) {
        DragEvent e{dragStart, location, location - dragStart, location - dragLast, false, dragging->frame};
        dragging->interaction.drag(e);
    }
    dragLast = location;
    bool inside = dragging->frame.contains(location);
    std::string next = inside ? dragging->interaction.id : "";
    if (next != pressedId) pressedId = next;
    dirty = true;
}

void Surface::mouseUp(Point location) {
    lastMouse = location;
    pressedId.clear();
    if (!dragging) return;
    Region r = *dragging;
    dragging.reset();
    dirty = true;
    if (r.interaction.release) r.interaction.release();
    if (dragMoved && r.interaction.drag) {
        DragEvent e{dragStart, location, location - dragStart, location - dragLast, true, r.frame};
        r.interaction.drag(e);
        return;
    }
    if (!r.frame.contains(location)) return;
    if (r.interaction.tapAt) r.interaction.tapAt(location);
    if (r.interaction.tap) r.interaction.tap();
}

void Surface::rightMouseDown(Point location) {
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (!it->frame.contains(location) || !it->interaction.enabled) continue;
        if (it->interaction.secondaryTap) {
            it->interaction.secondaryTap();
            dirty = true;
            return;
        }
    }
}

bool Surface::scrollWheel(Point location, double dx, double dy) {
    for (auto it = scrollRegions.rbegin(); it != scrollRegions.rend(); ++it) {
        if (!it->second.contains(location)) continue;
        auto& s = scrolls[it->first];
        double maxX = std::max(0.0, s.content.width - s.viewport.width), maxY = std::max(0.0, s.content.height - s.viewport.height);
        if (maxX <= 0 && maxY <= 0) continue;
        Point next{std::clamp(s.offset.x - dx, 0.0, maxX), std::clamp(s.offset.y - dy, 0.0, maxY)};
        if (next == s.offset) continue;
        s.offset = next;
        s.activity = seconds();
        dirty = true;
        return true;
    }
    return false;
}

void Surface::setFocusedField(const std::string& id) {
    if (focusedId == id) return;
    focusedId = id;
    dirty = true;
}

bool Surface::key(const KeyEvent& event) {
    if (keyHandler && keyHandler(event)) {
        dirty = true;
        return true;
    }
    return false;
}

std::optional<Element> Surface::find(const std::string& id) const {
    for (const auto& e : registered)
        if (e.id == id) return e;
    return std::nullopt;
}

std::vector<Element> Surface::matching(const std::string& prefix) const {
    std::vector<Element> out;
    for (const auto& e : registered)
        if (e.id.rfind(prefix, 0) == 0) out.push_back(e);
    return out;
}

bool Surface::contains(const std::string& label) const {
    for (const auto& e : registered)
        if (e.label == label) return true;
    return false;
}

bool Surface::tap(const std::string& id) {
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (it->interaction.id != id || !it->interaction.enabled) continue;
        Interaction i = it->interaction;
        Point centre{it->frame.midX(), it->frame.midY()};
        if (i.tapAt) i.tapAt(centre);
        if (i.tap) i.tap();
        if (!i.tap && !i.tapAt) continue;
        dirty = true;
        return true;
    }
    return false;
}

bool Surface::drag(const std::string& id, Point from, Point to) {
    for (auto it = regions.rbegin(); it != regions.rend(); ++it) {
        if (it->interaction.id != id || !it->interaction.drag) continue;
        Interaction i = it->interaction;
        i.drag({from, to, to - from, to - from, false, it->frame});
        i.drag({from, to, to - from, {0, 0}, true, it->frame});
        dirty = true;
        return true;
    }
    return false;
}

NativeField* Surface::field(const std::string& id) {
    for (auto& f : nativeFields)
        if (f.id == id) return &f;
    return nullptr;
}

}
