#pragma once

#include "Interface/Node.hpp"

#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ui {

struct KeyEvent {
    std::string characters;
    int keyCode = 0;
    bool command = false;
    bool shift = false;
    bool option = false;
    bool control = false;
    bool repeat = false;
};

struct Element {
    std::string id;
    std::string label;
    Frame frame;
    bool enabled = true;
};

class Surface {
public:
    explicit Surface(std::function<NodePtr()> build);

    static Surface* active();

    void resize(Size size, double scale);
    Size size() const { return bounds; }
    double scale() const { return factor; }
    void render(Canvas& canvas);
    void layout();
    void invalidate() { dirty = true; }
    bool needsDisplay() const { return dirty; }
    void requestFrame() { animating = true; }
    bool wantsFrame() const { return animating; }

    void mouseMoved(Point location);
    void mouseDown(Point location, int clicks = 1);
    void mouseDragged(Point location);
    void mouseUp(Point location);
    void rightMouseDown(Point location);
    void mouseExited();
    bool scrollWheel(Point location, double dx, double dy);
    void setFocusedField(const std::string& id);
    bool focused(const std::string& id) const { return !id.empty() && focusedId == id; }
    bool key(const KeyEvent& event);
    Cursor cursor() const { return currentCursor; }
    bool covers(Point location) const;
    std::optional<std::vector<MenuItem>> menuAt(Point location) const;
    std::optional<std::pair<std::vector<MenuItem>, Frame>> pullDownAt(Point location) const;
    std::optional<std::vector<MenuItem>> menu(const std::string& id) const;
    void setModifiers(Modifiers value) { held = value; }
    const Modifiers& modifiers() const { return held; }
    std::string tooltip() const;

    std::function<bool(const KeyEvent&)> keyHandler;

    bool hovered(const std::string& id) const { return hover.count(id) > 0; }
    bool pressed(const std::string& id) const { return pressedId == id; }
    Point scrollOffset(const std::string& key) const;
    void setScrollOffset(const std::string& key, Point offset, Size viewport, Size content);
    void scrollTo(const std::string& key, Point offset);
    bool scrollActive(const std::string& key) const;

    const std::vector<NativeField>& fields() const { return nativeFields; }
    const std::vector<ViewportSlot>& viewports() const { return slots; }
    const std::vector<Element>& elements() const { return registered; }
    std::optional<Element> find(const std::string& id) const;
    struct RegionInfo {
        std::string id;
        Frame frame;
        bool enabled = true;
        bool actionable = false;
        bool hovers = false;
        bool watches = false;
        Cursor cursor = Cursor::arrow;
    };
    std::vector<RegionInfo> regionInfo() const;
    Cursor cursorAt(Point location) const;
    std::vector<Element> matching(const std::string& prefix) const;
    bool contains(const std::string& label) const;
    bool tap(const std::string& id);
    bool drag(const std::string& id, Point from, Point to);
    NativeField* field(const std::string& id);
    NodePtr root() const { return tree; }

    void pushClip(const Frame& frame);
    void popClip();
    Frame visible(const Frame& frame) const;
    Frame clipFrame() const;
    void registerInteraction(const Interaction& interaction, const Frame& frame);
    void registerElement(const Node& node);
    void registerField(const NativeField& field);
    void registerViewport(const ViewportSlot& slot);
    void registerScroll(const std::string& key, const Frame& frame);

    Environment environment;

private:
    struct Region {
        Interaction interaction;
        Frame frame;
    };
    struct ScrollState {
        Point offset;
        Size viewport;
        Size content;
        Frame frame;
        double activity = 0;
    };

    const Region* hit(Point location, bool needsTap) const;
    void updateHover(Point location);
    void collect();

    std::function<NodePtr()> build;
    NodePtr tree;
    Size bounds;
    double factor = 1;
    bool dirty = true;
    bool animating = false;
    std::vector<Region> regions;
    std::vector<Frame> clips;
    std::vector<NativeField> nativeFields;
    std::vector<ViewportSlot> slots;
    std::vector<Element> registered;
    std::map<std::string, ScrollState> scrolls;
    std::vector<std::pair<std::string, Frame>> scrollRegions;
    std::set<std::string> hover;
    std::string pressedId;
    std::string focusedId;
    std::optional<Region> dragging;
    Point dragStart;
    Point dragLast;
    bool dragMoved = false;
    Cursor currentCursor = Cursor::arrow;
    Point lastMouse{-1, -1};
    Modifiers held;
};

}
