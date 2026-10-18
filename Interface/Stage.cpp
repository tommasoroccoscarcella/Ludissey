#include "Stage.hpp"

namespace ui {

std::shared_ptr<Stage> Stages::find(const std::string& id) const {
    auto it = stages.find(id);
    return it == stages.end() ? nullptr : it->second;
}

void Stages::begin() { used.clear(); }

void Stages::finish() {
    for (auto it = stages.begin(); it != stages.end();) {
        if (used.count(it->first)) {
            ++it;
            continue;
        }
        it->second->stop();
        if (focused == it->first) focused.clear();
        it = stages.erase(it);
    }
}

void Stages::tick(double now, double delta) {
    auto copy = stages;
    for (auto& [id, stage] : copy) stage->tick(now, delta);
}

std::vector<ld::render::View> Stages::views(const std::vector<ViewportSlot>& slots) {
    std::vector<ld::render::View> out;
    for (const auto& slot : slots) {
        auto stage = find(slot.id);
        if (!stage || !stage->scene() || !stage->camera()) continue;
        stage->size = {slot.frame.width, slot.frame.height};
        ld::render::View v;
        v.key = slot.id;
        v.scene = stage->scene();
        v.camera = stage->camera();
        v.x = slot.frame.x;
        v.y = slot.frame.y;
        v.width = slot.frame.width;
        v.height = slot.frame.height;
        v.quality = stage->quality();
        out.push_back(v);
    }
    return out;
}

bool Stages::pointer(const std::string& id, const PointerEvent& event) {
    auto stage = find(id);
    if (!stage) return false;
    if (event.phase == PointerPhase::down || event.phase == PointerPhase::rightDown) focused = id;
    return stage->pointer(event, stage->size);
}

bool Stages::key(const KeyInput& key) {
    auto stage = find(focused);
    return stage && stage->key(key);
}

bool Stages::continuous() const {
    for (const auto& [id, stage] : stages)
        if (stage->continuous()) return true;
    return false;
}

void Stages::resigned() {
    if (auto stage = find(focused)) stage->resigned();
}

}
