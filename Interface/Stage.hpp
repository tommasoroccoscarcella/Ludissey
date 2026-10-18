#pragma once

#include "Engine/Rendering/Renderer.hpp"
#include "Interface/Node.hpp"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ui {

enum class PointerPhase { down, dragged, up, moved, scroll, rightDown, rightDragged, rightUp, otherDown, otherDragged, otherUp, exited };

struct PointerEvent {
    PointerPhase phase = PointerPhase::moved;
    Point location;
    Point delta;
    double scrollX = 0;
    double scrollY = 0;
    bool precise = false;
    int clicks = 1;
    bool command = false;
    bool shift = false;
    bool option = false;
    bool control = false;
};

struct KeyInput {
    int keyCode = 0;
    std::string characters;
    bool down = true;
    bool repeat = false;
    bool command = false;
    bool shift = false;
    bool option = false;
    bool control = false;
};

class Stage {
public:
    virtual ~Stage() = default;
    virtual void tick(double now, double delta) {}
    virtual const ld::render::Scene* scene() const = 0;
    virtual const ld::render::Node* camera() const = 0;
    virtual ld::render::Quality quality() const { return {}; }
    virtual bool pointer(const PointerEvent& event, Size viewport) { return false; }
    virtual bool key(const KeyInput& key) { return false; }
    virtual bool continuous() const { return true; }
    virtual void resigned() {}
    virtual void stop() {}
    Size size;
};

class Stages {
public:
    std::shared_ptr<Stage> find(const std::string& id) const;
    void begin();
    void finish();
    void tick(double now, double delta);
    std::vector<ld::render::View> views(const std::vector<ViewportSlot>& slots);
    bool pointer(const std::string& id, const PointerEvent& event);
    bool key(const KeyInput& key);
    bool continuous() const;
    void resigned();
    std::string focused;

private:
    std::map<std::string, std::shared_ptr<Stage>> stages;
    std::set<std::string> used;
};

}
