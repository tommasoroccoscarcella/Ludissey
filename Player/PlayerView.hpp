#pragma once

#include "Engine/Client/Store.hpp"
#include "Engine/Multiplayer/PlayerSession.hpp"
#include "Engine/Rendering/PlayerScene.hpp"
#include "Engine/Rendering/Quality.hpp"
#include "Interface/Components.hpp"
#include "Interface/GuiOverlay.hpp"
#include "Interface/Stage.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>

namespace player {

class PlayerShell {
public:
    explicit PlayerShell(std::shared_ptr<ld::Store> store);
    ~PlayerShell();
    ui::NodePtr build();
    void tick(double now, double delta);
    std::vector<ld::render::View> views(const std::vector<ui::ViewportSlot>& slots);
    bool pointer(const std::string& slot, const ui::PointerEvent& event);
    bool key(const ui::KeyInput& key);
    void resigned();
    bool active() const { return session != nullptr; }
    std::function<void()> invalidated;
    std::function<void(const std::string&)> focusField;
    std::function<void()> focusViewport;
    std::function<ui::Image(const std::string&)> experienceIcon;
    std::shared_ptr<ld::PlayerGameplay> gameplay = std::make_shared<ld::PlayerGameplay>();
    void chooseGraphics(std::optional<int> level);

private:
    std::shared_ptr<ld::Store> store;
    std::shared_ptr<ld::PlayerSession> session;
    std::unique_ptr<ld::render::PlayerScene> scene;
    std::string signature;
    std::optional<ld::JoinTicket> current;
    ld::render::GraphicsQuality quality;
    ui::GuiOverlay overlay;
    std::string draft;
    bool confirmingLeave = false;
    bool chatWasFocused = false;
    std::size_t chatCount = 0;
    std::size_t chatSeen = 0;
    std::set<std::string> muted;
    ui::Point last;
    double clock = 0;

    void invalidate();
    void begin();
    void leave();
    void prepareScene();
    ui::NodePtr stage();
    ui::NodePtr loading(const std::optional<std::string>& text);
    ui::NodePtr topbar(const std::string& title, const std::string& username);
    ui::NodePtr hotbar();
    ui::NodePtr healthBar();
    ui::NodePtr roster();
    ui::NodePtr chat();
    ui::NodePtr notice(const std::string& text);
    ui::NodePtr menu();
    ui::NodePtr menuButton(const std::string& id, const std::string& title, bool primary, std::function<void()> action);
};

}
