#pragma once

#include "Engine/Core/Json.hpp"

#include "Engine/DataModel/DataModel.hpp"
#include "Engine/Scripting/EngineTypes.hpp"
#include "Engine/Terrain/Terrain.hpp"
#include "Lim/VM/Vm.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ld {

struct OutputLine {
    enum class Kind { info, warning, error };
    std::string text;
    Kind kind = Kind::info;
    std::optional<std::string> source;
    std::optional<std::uint32_t> line;
    double time = 0;
    bool operator==(const OutputLine& other) const { return text == other.text && kind == other.kind && source == other.source; }
    bool error() const { return kind == Kind::error; }
    std::string stamped() const;
};

struct CameraState {
    Vec3 position;
    Vec3 focus;
    double fieldOfView = 70;
    bool firstPerson = false;
};

struct ScriptTween {
    InstancePtr target;
    std::map<std::string, Variant> goals;
    std::map<std::string, Variant> starts;
    double duration = 1;
    std::string style = "Quad";
    std::string direction = "Out";
    double elapsed = 0;
    bool finished = false;
    std::function<void()> callback;
    static double ease(double t, const std::string& style, const std::string& direction);
    static std::optional<Variant> mix(const Variant& a, const Variant& b, double t);
};

struct ScriptMetric {
    std::string script;
    ScriptContext context = ScriptContext::server;
    double seconds = 0;
    std::uint64_t events = 0;
    std::uint64_t frames = 0;
};

struct ScriptPerformance {
    std::vector<ScriptMetric> scripts;
    std::map<ScriptContext, long long> memory;
    double elapsed = 0;
};

class ScriptEngine {
public:
    static constexpr double slice = 0.25;
    static constexpr double frameBudget = 0.008;
    static constexpr double minimumWait = 1.0 / 30;
    static constexpr int remoteRate = 120;
    static constexpr std::size_t remotePayload = 64 * 1024;

    ScriptEngine(DataModel& model, std::function<void(const OutputLine&)> output);
    ~ScriptEngine();
    ScriptEngine(const ScriptEngine&) = delete;
    ScriptEngine& operator=(const ScriptEngine&) = delete;

    DataModel& model;
    std::function<void(const OutputLine&)> output;
    bool studio = false;
    std::set<std::string> keys;
    InstancePtr localPlayer;
    InstancePtr focusedTextBox;
    std::vector<InstancePtr> selection;
    std::function<void(const InstancePtr&, bool)> soundPlayer;
    std::function<CameraState()> camera;
    Terrain* terrain = nullptr;
    using StorageReply = std::function<void(std::optional<Json> value, std::string error)>;
    std::function<void(const std::string& action, const Json& value, StorageReply reply)> storage;

    double clock() const { return time; }
    int running() const;
    void launch(const InstancePtr& script, ScriptContext context);
    bool execute(const std::string& source, ScriptContext context, const std::string& label);
    void step(double delta);
    void fire(const InstancePtr& node, const std::string& name, const std::vector<Variant>& arguments, std::optional<ScriptContext> context = std::nullopt);
    void schedule(double after, std::function<void()> action);
    void playTween(const InstancePtr& tween);
    void cancelTween(const InstancePtr& tween);
    void startTween(std::shared_ptr<ScriptTween> tween);
    void translate(const InstancePtr& model, Vec3 target);
    struct Hit {
        InstancePtr part;
        Vec3 point;
        double distance = 0;
    };
    Hit raycast(Vec3 origin, Vec3 direction, const std::vector<InstancePtr>& ignoring) const;
    void stop();
    ScriptPerformance performance() const;
    lim::Vm* vm(ScriptContext context);
    void emit(OutputLine line);

    struct Implementation;
    std::unique_ptr<Implementation> implementation;

private:
    double time = 0;
    std::vector<std::pair<double, std::function<void()>>> timers;
    std::vector<std::shared_ptr<ScriptTween>> tweens;
    void advanceTweens(double delta);
};

}
