#pragma once

#include "Engine/Rendering/Renderer.hpp"
#include "Interface/Stage.hpp"
#include "Interface/Surface.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace platform {

using PointerPhase = ui::PointerPhase;
using PointerEvent = ui::PointerEvent;
using KeyInput = ui::KeyInput;

struct WindowSpec {
    std::string title;
    double width = 1180;
    double height = 760;
    double minimumWidth = 860;
    double minimumHeight = 680;
    bool sheet = false;
};

struct FrameSample {
    double interval = 0;
    double total = 0;
    double tick = 0;
    double interface = 0;
    double present = 0;
    bool interfaceRendered = false;
};

class Window {
public:
    explicit Window(WindowSpec spec, std::function<ui::NodePtr()> build);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    ui::Surface& surface();
    void setTitle(const std::string& title);
    void setEdited(bool edited);
    void invalidate();
    void show();
    void focusViewport();
    void beginSheet(Window& sheet);
    void endSheet(Window& sheet);
    void focusField(const std::string& id);
    bool editingText() const;
    double scale() const;

    std::function<void(double time, double delta)> tick;
    std::function<std::vector<ld::render::View>(const std::vector<ui::ViewportSlot>& slots)> views;
    std::function<bool(const std::string& slot, const PointerEvent& event)> pointer;
    std::function<bool(const KeyInput& key)> keys;
    std::function<void()> resignedViewport;
    std::function<void(const ui::Surface&)> rendered;
    std::function<bool()> continuous;
    std::function<void(const FrameSample&)> sampled;
    std::function<bool()> closing;

    struct Native;

private:
    std::unique_ptr<Native> native;
};

struct MenuItem {
    std::string title;
    std::string key;
    std::string selector;
    std::function<void()> action;
};

struct Menu {
    std::string title;
    std::vector<MenuItem> items;
};

struct Application {
    std::string bundleIdentifier;
    std::vector<Menu> menus;
    std::function<void()> launched;
    std::function<void(const std::string& url)> openUrl;
    std::function<void()> reopen;
    std::function<void()> terminating;
    std::function<bool()> quitting;
};

int run(Application application);
void installAdapters();
void installAudio();
void terminate();
bool sendAction(const std::string& selector);
std::string version();
std::string bundleIdentifier();
std::filesystem::path bundleResources();
std::filesystem::path bundlePath();
void openUrl(const std::string& url);
void openApplication(const std::filesystem::path& bundle, const std::vector<std::string>& urls);
std::optional<std::filesystem::path> chooseFile(const std::vector<std::string>& extensions, const std::string& prompt);
std::optional<std::filesystem::path> chooseDestination(const std::string& name, const std::string& extension);
std::string clipboard();
void activate();
bool singleInstance(const std::string& identifier);

}
