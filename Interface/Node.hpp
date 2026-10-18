#pragma once

#include "Interface/Canvas.hpp"
#include "Interface/Geometry.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ui {

class Node;
using NodePtr = std::shared_ptr<Node>;
class Surface;

enum class Axis { horizontal, vertical };
enum class Cursor { arrow, pointer, text, resizeHorizontal, resizeVertical, crosshair, move };

struct Environment {
    std::optional<Font> font;
    std::optional<Color> foreground;
    bool disabled = false;
    bool compact = false;
    std::optional<TextAlign> multilineAlignment;
};

struct DragEvent {
    Point start;
    Point location;
    Point translation;
    Point delta;
    bool ended = false;
    Frame bounds;
};

struct MenuItem {
    std::string title;
    std::string icon;
    std::string shortcut;
    bool enabled = true;
    bool separator = false;
    bool checked = false;
    std::function<void()> action;
    std::vector<MenuItem> submenu;
    static MenuItem divider() {
        MenuItem item;
        item.separator = true;
        return item;
    }
};

struct Modifiers {
    bool command = false;
    bool shift = false;
    bool option = false;
    bool control = false;
};

struct Interaction {
    std::string id;
    std::function<void()> tap;
    std::function<void()> doubleTap;
    std::function<void(Point)> tapAt;
    std::function<void(const DragEvent&)> drag;
    std::function<void(Point)> press;
    std::function<void()> release;
    std::function<void(bool)> hover;
    std::function<void()> secondaryTap;
    bool tracksHover = false;
    std::string help;
    std::function<std::vector<MenuItem>()> menu;
    Cursor cursor = Cursor::arrow;
    bool enabled = true;
    bool pullDown = false;
};

struct HighlightSpan {
    std::size_t start = 0;
    std::size_t length = 0;
    Color color;
    bool bold = false;
};

enum class FindAction { none, find, replace, next, previous };

struct EditorReply {
    std::optional<std::string> text;
    std::optional<std::pair<std::size_t, std::size_t>> selection;
    std::string message;
};

struct EditorCommand {
    std::string title;
    std::string prompt;
    bool changesText = false;
    std::function<EditorReply(const std::string&, std::size_t, const std::string&)> run;
};

struct NativeField {
    std::string id;
    Frame frame;
    Frame full;
    Frame clip;
    std::string text;
    std::string prompt;
    Font font;
    Color color;
    TextAlign alignment = TextAlign::leading;
    bool secure = false;
    bool multiline = false;
    bool code = false;
    bool bordered = false;
    bool editable = true;
    std::size_t limit = 0;
    std::optional<int> errorLine;
    std::set<std::uint32_t> breakpoints;
    FindAction find = FindAction::none;
    std::function<std::vector<HighlightSpan>(const std::string&)> highlight;
    std::function<std::vector<std::string>(const std::string&, std::size_t)> complete;
    std::function<std::string(const std::string&, std::size_t)> inspect;
    std::vector<EditorCommand> commands;
    std::function<void()> editBoundary;
    std::function<void(bool redo)> historyAction;
    std::function<bool(bool redo)> historyEnabled;
    std::vector<std::string> options;
    int selectedOption = -1;
    std::function<void(int)> choose;
    std::function<void(const std::string&)> changed;
    std::function<void(const std::string&)> commit;
    std::function<void()> cancel;
    std::function<void(bool)> focus;
    std::optional<int> revealLine;
};

struct ViewportSlot {
    std::string id;
    Frame frame;
};

class Node : public std::enable_shared_from_this<Node> {
public:
    virtual ~Node() = default;
    Frame frame;
    Interaction interaction;
    bool interactive = false;
    std::vector<NodePtr> children;
    std::string accessibility;
    std::string label;
    bool selected = false;

    virtual Size measure(Proposal proposal);
    virtual void place(Frame bounds);
    virtual void draw(Canvas& canvas, Surface& surface);
    virtual void resolve(const Environment& environment);
    virtual bool expandsIn(Axis) const { return false; }
    Size size(Proposal proposal);
    void drawChildren(Canvas& canvas, Surface& surface);
    void clearCache() { cache.clear(); }

    NodePtr padding(double all);
    NodePtr padding(Insets insets);
    NodePtr paddingHorizontal(double value);
    NodePtr paddingVertical(double value);
    NodePtr frameSize(std::optional<double> width, std::optional<double> height, Alignment alignment = Alignment::center());
    NodePtr flexible(std::optional<double> minWidth, std::optional<double> maxWidth, std::optional<double> minHeight, std::optional<double> maxHeight, Alignment alignment = Alignment::center());
    NodePtr fillWidth(Alignment alignment = Alignment::center());
    NodePtr fill(Alignment alignment = Alignment::center());
    NodePtr fixed(bool horizontal = true, bool vertical = true);
    NodePtr background(NodePtr node, Alignment alignment = Alignment::center());
    NodePtr background(Color color, double radius = 0);
    NodePtr overlay(NodePtr node, Alignment alignment = Alignment::center());
    NodePtr border(Color color, double width = 1, double radius = 0);
    NodePtr clipped(double radius = 0);
    NodePtr opacity(double value);
    NodePtr offset(double dx, double dy);
    NodePtr shadow(Color color, double dx, double dy, double blur = 0);
    NodePtr boxShadow(Color color, double dx, double dy, double blur, double radius);
    NodePtr font(Font font);
    NodePtr foreground(Color color);
    NodePtr disabled(bool value = true);
    NodePtr compact(bool value = true);
    NodePtr multilineAlignment(TextAlign alignment);
    NodePtr onTap(std::string id, std::function<void()> action, Cursor cursor = Cursor::arrow);
    NodePtr onHover(std::string id, std::function<void(bool)> hover);
    NodePtr onDrag(std::string id, std::function<void(const DragEvent&)> drag, Cursor cursor = Cursor::arrow);
    NodePtr id(std::string identifier);
    NodePtr named(std::string text);
    NodePtr hidden(bool value = true);
    NodePtr help(std::string text);
    NodePtr contextMenu(std::function<std::vector<MenuItem>()> items);
    NodePtr pullDownMenu(std::string id, std::function<std::vector<MenuItem>()> items);

protected:
    struct Cached {
        std::optional<double> width, height;
        Size size;
    };
    std::vector<Cached> cache;
};

NodePtr text(std::string value, std::optional<Font> font = std::nullopt, std::optional<Color> color = std::nullopt);
NodePtr styledText(std::vector<TextRun> runs);
NodePtr lineLimited(NodePtr textNode, int lines, Truncation truncation = Truncation::tail);
NodePtr scaledText(NodePtr textNode, double minimum);
NodePtr hstack(std::vector<NodePtr> children, double spacing = 8, VAlign alignment = VAlign::center);
NodePtr vstack(std::vector<NodePtr> children, double spacing = 8, HAlign alignment = HAlign::center);
NodePtr zstack(std::vector<NodePtr> children, Alignment alignment = Alignment::center());
NodePtr spacer(std::optional<double> minimum = std::nullopt);
NodePtr divider(Color color = Color::black(0.16));
NodePtr rectangle(Color fill);
NodePtr roundedRectangle(double radius, Color fill);
NodePtr strokeRectangle(Color stroke, double width = 1, double radius = 0);
NodePtr circle(Color fill);
NodePtr ringShape(Color stroke, double width);
NodePtr gradient(Gradient value, double radius = 0);
NodePtr empty();
NodePtr image(Image picture, double width, double height, std::optional<Color> tint = std::nullopt, double opacity = 1);
NodePtr templateImage(Image picture, double width, double height);
NodePtr picture(Image image, bool fill, double opacity = 1);
NodePtr reader(std::function<NodePtr(const Environment&)> build);
NodePtr scroll(std::string id, NodePtr content, Axis axis = Axis::vertical, bool indicators = true);
NodePtr grid(std::vector<NodePtr> items, double minimum, double maximum, double spacing, double rowSpacing);
NodePtr columns(std::vector<NodePtr> items, int count, double spacing, double rowSpacing);
NodePtr geometry(std::function<NodePtr(Size)> build);
NodePtr canvas(std::function<void(Canvas&, const Frame&)> draw, std::optional<Size> ideal = std::nullopt);
NodePtr textField(NativeField field);
NodePtr popup(NativeField field);
NodePtr viewport(std::string id);
NodePtr when(bool condition, NodePtr node);

}
