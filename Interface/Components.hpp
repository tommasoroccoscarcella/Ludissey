#pragma once

#include "Interface/Node.hpp"
#include "Interface/Theme.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ui {

enum class ButtonRole { primary, secondary, destructive, success, warning, dark };
enum class ButtonSize { small, normal, large };

NodePtr classicBorder(NodePtr content, double radius = theme::metrics::controlRadius, bool strong = false);
NodePtr embossed(NodePtr node, const theme::Emboss& relief);
NodePtr button(std::string id, NodePtr label, std::function<void()> action, ButtonRole role = ButtonRole::secondary, ButtonSize size = ButtonSize::normal);
NodePtr button(std::string id, std::string title, std::function<void()> action, ButtonRole role = ButtonRole::secondary, ButtonSize size = ButtonSize::normal);
NodePtr backButton(std::string id, std::string title, std::function<void()> action);
NodePtr barButton(std::string id, NodePtr content, std::function<void()> action, bool on = false);
NodePtr playButton(std::string id, std::string title, std::function<void()> action);
NodePtr linkButton(std::string id, std::string title, std::function<void()> action);
NodePtr plainButton(std::string id, NodePtr label, std::function<void()> action, Cursor cursor = Cursor::arrow);
NodePtr styledField(NativeField field);
NodePtr select(NativeField field);
NodePtr segments(std::string id, const std::vector<std::string>& titles, int selected, std::function<void(int)> choose, bool small = false, const std::vector<std::string>& keys = {});
NodePtr glossBackground(ButtonRole role, double radius, bool sunk = false);
NodePtr toggle(std::string id, bool on, std::function<void(bool)> change);
NodePtr icon(const std::string& name, double size = theme::iconSize::toolbar, const std::string& fallback = "");
NodePtr glyph(const std::string& name, double size = theme::iconSize::toolbar, std::optional<Color> color = std::nullopt);
NodePtr symbol(const std::string& text, double size = 13);
NodePtr checkbox(std::string id, NodePtr label, bool on, std::function<void(bool)> change);
NodePtr checkbox(std::string id, std::string title, bool on, std::function<void(bool)> change);
NodePtr logo(double height, std::string label = "Ludissey");
NodePtr loading(std::string message = "");
NodePtr slider(std::string id, double value, double low, double high, double step, Color accent, std::function<void(double)> change, Color track = Color::white(0.25));
NodePtr radio(std::string id, const std::vector<std::string>& titles, int selected, std::function<void(int)> choose);
NodePtr pageTabs(std::string id, const std::vector<std::string>& titles, int selected, std::function<void(int)> choose);
NodePtr classicTabs(std::string id, const std::vector<std::string>& titles, int selected, std::function<void(int)> choose);
NodePtr card(NodePtr content, const std::string& hoverId = "");
NodePtr emptyState(const std::string& iconName, const std::string& sentence, NodePtr action = nullptr);
NodePtr group(std::vector<NodePtr> rows);
NodePtr groupRow(NodePtr label, NodePtr accessory = nullptr, std::string detail = "");
NodePtr groupTitle(const std::string& title);
NodePtr groupNote(const std::string& note);
NodePtr disclosure(std::string value = "");
NodePtr presence(bool online, double side);
NodePtr unreadDot(double side);
NodePtr appIcon(Image picture, const std::string& genre, double side);
NodePtr progressBar(double fraction);
NodePtr blackBar();
NodePtr stripes();
NodePtr navigationBar();
NodePtr recess();
NodePtr sheet(const theme::Sheen& colors, bool bottomEdge = true);
NodePtr frameImage(NodePtr content, double radius = theme::metrics::frameRadius);
NodePtr reflected(Image picture, double width, double height);
NodePtr counter(int count, bool warning = false);
Color genreColor(const std::string& genre);
NodePtr genreTile(const std::string& genre);
void drawMark(Canvas& canvas, const Frame& rect, Color color);

struct DialogButton {
    std::string label;
    ButtonRole role = ButtonRole::primary;
    std::function<void()> action;
    std::string id = "";
};

NodePtr dialog(const std::string& title, const std::string& message, std::vector<DialogButton> buttons, bool warning = false, NodePtr art = nullptr);
NodePtr checkBadge(double side);
NodePtr popover(NodePtr content, double width, const std::string& title, NodePtr accessory = nullptr, double arrow = -32);
NodePtr menuRow(std::string id, NodePtr label, std::function<void()> action);

namespace icons {
const std::map<std::string, std::string>& semanticFiles();
const std::map<std::string, std::string>& placeholders();
bool packed(const std::string& name);
std::string file(const std::string& name);
std::string placeholder(const std::string& name);
std::filesystem::path folder();
Image load(const std::string& name);
Image sized(const std::string& name, int pixels);
}

}
