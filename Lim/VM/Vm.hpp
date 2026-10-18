#pragma once

#include "Lim/Diagnostics/Diagnostic.hpp"
#include "Value.hpp"

#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace lim {

struct Environment {
    std::unordered_map<std::string, Value> values;
    std::unordered_map<std::string, bool> mutableNames;
    std::string name;
    void release();
};

struct Frame {
    std::shared_ptr<ClosureObject> closure;
    std::shared_ptr<Environment> environment;
    std::size_t ip = 0;
    std::size_t base = 0;
    std::size_t callee = 0;
};

struct Fiber : std::enable_shared_from_this<Fiber> {
    enum class State { ready, running, waiting, done, failed };
    std::vector<Value> stack;
    std::vector<Frame> frames;
    State state = State::ready;
    Suspension wait;
    double waitStarted = 0;
    std::shared_ptr<TaskObject> task;
    std::string label;
    std::uint64_t id = 0;
    bool synchronous = false;
    Value connection;
    std::uint32_t lastLine = 0;
    std::size_t lastDepth = 0;
};

enum class StepMode { run, into, over, out };

struct DebugVariable {
    std::string name;
    std::string value;
};

struct DebugFrame {
    std::string function;
    std::string file;
    std::uint32_t line = 0;
    std::vector<DebugVariable> locals;
};

struct DebugStop {
    std::uint64_t fiber = 0;
    std::string reason;
    std::vector<DebugFrame> frames;
};

struct DebugSettings {
    std::map<std::string, std::set<std::uint32_t>> breakpoints;
    std::function<void(const DebugStop&)> stopped;
};

class Host {
public:
    virtual ~Host() = default;
    virtual std::optional<Value> global(Vm& vm, std::string_view name);
    virtual Value getMember(Vm& vm, const Value& object, const std::string& name);
    virtual void setMember(Vm& vm, const Value& object, const std::string& name, const Value& value);
    virtual Value connect(Vm& vm, const Value& event, std::function<void(std::vector<Value>)> handler, bool once);
    virtual std::string describe(const Value& object);
    virtual bool isA(const Value& object, std::string_view type);
    virtual std::optional<Value> importModule(Vm& vm, const std::string& name);
};

struct OutputMessage {
    Severity severity = Severity::hint;
    std::string text;
    std::string source;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::string stack;
};

struct Limits {
    std::uint64_t instructionsPerSlice = 20'000'000;
    double sliceSeconds = 4;
    long long memoryBytes = 128LL * 1024 * 1024;
    std::size_t maximumFrames = 200;
    std::size_t maximumStack = 1u << 20;
    std::size_t maximumFibers = 20000;
    int maximumNesting = 24;
};

struct ScriptUsage {
    double seconds = 0;
    std::uint64_t events = 0;
    std::uint64_t frames = 0;
};

struct VmStatistics {
    std::uint64_t instructions = 0;
    std::uint64_t fibersCreated = 0;
    std::uint64_t errors = 0;
    std::uint64_t events = 0;
    double scriptSeconds = 0;
};

class Vm {
public:
    explicit Vm(Limits limits = {});
    ~Vm();
    Vm(const Vm&) = delete;
    Vm& operator=(const Vm&) = delete;

    void setHost(Host* host) { hostInterface = host; }
    Host* host() const { return hostInterface; }
    void setOutput(std::function<void(const OutputMessage&)> output) { outputSink = std::move(output); }
    void defineBuiltin(const std::string& name, Value value);

    std::shared_ptr<Environment> newEnvironment(const std::string& name, std::map<std::string, Value> preset = {});
    std::shared_ptr<Fiber> run(std::shared_ptr<const Module> module, std::shared_ptr<Environment> environment, const std::string& label);
    Value runModule(std::shared_ptr<const Module> module, std::shared_ptr<Environment> environment, const std::string& label, std::string* error);
    std::shared_ptr<Fiber> spawn(const Value& function, std::vector<Value> arguments, const std::string& label);
    std::shared_ptr<Fiber> dispatch(const Value& function, std::vector<Value> arguments, const std::string& label);
    Value callSync(const Value& function, std::vector<Value> arguments);
    void step(double delta);
    double clock() const { return time; }
    std::size_t running() const;
    void stopAll();
    const VmStatistics& statistics() const { return stats; }
    const Limits& limits() const { return bounds; }

    void emit(Severity severity, const std::string& text, const Fiber* fiber = nullptr);
    Value text(std::string value);
    Value list(std::vector<Value> items = {});
    Value map();
    Value native(std::string name, NativeFunction function, bool async = false);
    Value boundNative(std::string name, Value receiver, NativeFunction function, bool async = false);
    Value hostObject(std::uint64_t handle, std::string className, std::string tag = {});
    std::shared_ptr<TaskObject> newTask();
    void completeTask(const std::shared_ptr<TaskObject>& task, Value result, bool failed = false, std::string error = {});
    std::string toText(const Value& value, int depth = 0);
    const std::string& textOf(const Value& value) const;
    long long memoryUsed() const { return account->load(); }
    void charge(Object& object, long long bytes);
    void register_(const std::shared_ptr<Object>& object);
    Value getMember(const Value& object, const std::string& name);
    void setMember(const Value& object, const std::string& name, const Value& value);
    Value toJsonValue(const std::string& text);
    std::string fromValueToJson(const Value& value, int depth = 0);
    std::shared_ptr<Fiber> currentFiber() const { return current; }
    Value suspend(Suspension suspension, std::function<void(Value)> then = {});
    void cancelTask(const std::shared_ptr<TaskObject>& task);
    const std::unordered_map<std::string, double>& scriptProfile() const { return profile; }
    std::map<std::string, ScriptUsage> scriptUsage() const;
    std::mt19937_64& random() { return generator; }
    void setDebugger(DebugSettings settings);
    void clearDebugger();
    bool debugging() const { return debugState != nullptr; }
    bool paused() const { return pausedFiber != nullptr; }
    void resume(StepMode mode);
    DebugStop inspect(const Fiber& fiber) const;

private:
    friend struct Interpreter;
    Limits bounds;
    Host* hostInterface = nullptr;
    std::function<void(const OutputMessage&)> outputSink;
    std::unordered_map<std::string, Value> builtins;
    std::vector<std::shared_ptr<Fiber>> fibers;
    std::shared_ptr<std::atomic<long long>> account;
    std::vector<std::weak_ptr<Object>> containers;
    std::vector<std::weak_ptr<Environment>> environments;
    std::shared_ptr<Fiber> current;
    struct Pending {
        std::shared_ptr<TaskObject> task;
        Suspension suspension;
        double started = 0;
        std::function<void(Value)> then;
        std::string label;
    };
    std::vector<Pending> pending;
    std::unordered_map<std::string, double> profile;
    std::unordered_map<std::string, std::uint64_t> eventCount;
    std::unordered_map<std::string, std::uint64_t> frameCount;
    double time = 0;
    std::uint64_t nextFiber = 1;
    int nesting = 0;
    VmStatistics stats;
    std::mt19937_64 generator{0x11D1};
    std::shared_ptr<DebugSettings> debugState;
    std::shared_ptr<Fiber> pausedFiber;
    StepMode stepMode = StepMode::run;
    std::uint64_t stepFiber = 0;
    std::size_t stepDepth = 0;
    bool debugStop(const std::shared_ptr<Fiber>& fiber);

    enum class Outcome { finished, suspended, failed };
    Outcome execute(const std::shared_ptr<Fiber>& fiber);
    bool prepareCall(Fiber& fiber, std::size_t callee, std::size_t count, const std::vector<std::string>* names, std::string& error, bool& suspended);
    void fail(const std::shared_ptr<Fiber>& fiber, const std::string& message);
    std::string location(const Fiber& fiber) const;
    std::string traceback(const Fiber& fiber) const;
    void finish(const std::shared_ptr<Fiber>& fiber, Value result);
    void schedule(const std::shared_ptr<Fiber>& fiber);
    void collectFinished();
};

void installLibrary(Vm& vm);

}
