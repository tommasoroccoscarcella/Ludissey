#pragma once

#include "Engine/DataModel/DataModel.hpp"

#include <array>
#include <string>
#include <vector>

namespace ld {

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
    bool null = false;
    static Rect make(double x, double y, double width, double height) { return {x, y, width, height, false}; }
    static Rect none() { return {0, 0, 0, 0, true}; }
    double minX() const { return w < 0 ? x + w : x; }
    double minY() const { return h < 0 ? y + h : y; }
    double width() const { return w < 0 ? -w : w; }
    double height() const { return h < 0 ? -h : h; }
    double maxX() const { return minX() + width(); }
    double maxY() const { return minY() + height(); }
    Rect intersection(const Rect& other) const;
    bool operator==(const Rect& other) const;
};

struct GuiNode {
    std::string id;
    std::string className;
    std::string name;
    PropertyMap properties;
    std::vector<GuiNode> children;

    double number(const std::string& key, double standard = 0) const;
    bool flag(const std::string& key, bool standard) const;
    std::string text(const std::string& key) const;
    std::string color(const std::string& key, const std::string& standard) const;
    std::string item(const std::string& key, const std::string& standard) const;
    std::array<double, 4> udim2(const std::string& key) const;
    std::array<double, 2> udim(const std::string& key) const;
    std::array<double, 2> vector2(const std::string& key) const;
    bool operator==(const GuiNode& other) const = default;
};

struct GuiElement {
    enum class Role { frame, label, button, box, image, imageButton, scroller };
    std::string id;
    Role role = Role::frame;
    Rect frame;
    Rect clip;
    std::string background;
    double backgroundAlpha = 1;
    std::string border;
    double borderSize = 1;
    std::string text;
    std::string hint;
    std::string textColor;
    double textAlpha = 1;
    double textSize = 14;
    std::string font;
    bool scaled = false;
    bool wrapped = false;
    std::string horizontal;
    std::string vertical;
    std::string image;
    std::string imageColor;
    double imageAlpha = 1;
};

std::string_view roleName(GuiElement::Role role);

namespace gui_snapshot {
const std::vector<std::string>& visual();
std::vector<GuiNode> capture(const InstancePtr& root, const ClassRegistry& registry = ClassRegistry::shared());
std::vector<GuiNode> capture(const Experience& world, const std::string& parent = "StarterGui", const ClassRegistry& registry = ClassRegistry::shared());
}

namespace gui_layout {
std::vector<GuiElement> resolve(const std::vector<GuiNode>& screens, double width, double height);
}

}
