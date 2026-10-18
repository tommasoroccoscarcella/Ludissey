#include "Lim/Compiler/Compiler.hpp"
#include "Lim/Parser/Parser.hpp"
#include "Lim/VM/Vm.hpp"
#include "Tests/Support/Timing.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <chrono>
#include <limits>
#include <thread>

using namespace lim;

namespace {

struct Run {
    std::vector<std::string> lines;
    std::vector<std::string> errors;
    std::vector<Diagnostic> diagnostics;
    bool compiled = false;
};

Run execute(const std::string& source, int frames = 0, ExecutionContext context = ExecutionContext::server) {
    Run run;
    TypeRegistry registry = standardRegistry();
    AnalyzeOptions options;
    options.context = context;
    options.file = "Prova.lim";
    auto result = compileSource(source, "Prova", registry, options);
    run.diagnostics = result.diagnostics;
    if (!result.module) return run;
    run.compiled = true;
    Vm vm;
    vm.setOutput([&](const OutputMessage& m) {
        if (m.severity == Severity::error) run.errors.push_back(m.text);
        else run.lines.push_back(m.text);
    });
    auto module = std::make_shared<const Module>(std::move(*result.module));
    vm.run(module, vm.newEnvironment("Prova"), "Prova.lim");
    for (int i = 0; i < frames; ++i) vm.step(1.0 / 30);
    return run;
}

std::string report(const std::vector<Diagnostic>& diagnostics) {
    std::string text;
    for (const auto& d : diagnostics) text += summary(d) + "\n";
    return text;
}

bool hasDiagnostic(const Run& run, const std::string& fragment) {
    for (const auto& d : run.diagnostics)
        if (d.message.find(fragment) != std::string::npos || d.help.find(fragment) != std::string::npos) return true;
    return false;
}

}

TEST_CASE("Hello world e interpolazione", "[lim]") {
    auto run = execute("print(\"Hello from Lim!\")\nlet name = \"Ada\"\nprint(\"\\(name) joined, \\(1 + 2) punti\")");
    INFO(report(run.diagnostics));
    REQUIRE(run.compiled);
    CHECK(run.lines == std::vector<std::string>{"Hello from Lim!", "Ada joined, 3 punti"});
}

TEST_CASE("Variabili, funzioni, cicli e collezioni", "[lim]") {
    auto run = execute(R"LIM(
let speed = 16
fn double(value) {
    return value * 2
}
fn add(a: Number, b: Number) -> Number {
    return a + b
}
var total = 0
for i in 1...10 {
    if i % 2 == 0 {
        continue
    }
    total += i
}
let values = [1, 2, 3]
values.append(4)
let score = ["Alice": 10, "Bob": 20]
var names = []
for key, value in score {
    names.append("\(key)=\(value)")
}
var countdown = 3
while countdown > 0 {
    countdown -= 1
    if countdown == 1 { break }
}
print(double(speed), add(1, 2.5), total, values.count, names.join(";"), countdown, values.map(fn (x) { return x * x }))
)LIM");
    INFO(report(run.diagnostics));
    REQUIRE(run.compiled);
    REQUIRE(run.errors.empty());
    CHECK(run.lines == std::vector<std::string>{"32 3.5 25 4 Alice=10;Bob=20 1 [1, 4, 9, 16]"});
}

TEST_CASE("Lim: chiusure e celle per iterazione", "[lim]") {
    auto run = execute(R"LIM(
fn counter() {
    var n = 0
    return fn () {
        n += 1
        return n
    }
}
let next = counter()
next()
next()
var callbacks = []
for i in 0..<3 {
    callbacks.append(fn () { return i * 10 })
}
fn fact(n: Integer) -> Integer {
    if n <= 1 { return 1 }
    return n * fact(n - 1)
}
print(next(), callbacks[0](), callbacks[2](), fact(10))
)LIM");
    INFO(report(run.diagnostics));
    REQUIRE(run.compiled);
    CHECK(run.errors.empty());
    CHECK(run.lines == std::vector<std::string>{"3 0 20 3628800"});
}

TEST_CASE("Classi, enum e match", "[lim]") {
    auto run = execute(R"LIM(
enum State {
    idle
    running
    jumping
}
class Weapon {
    var damage: Number = 25
    let name: Text = "Spada"
    fn fire(target: Weapon) {
        target.damage -= damage
    }
    fn describe() -> Text {
        return "\(name) \(self.damage)"
    }
}
let a = Weapon()
let b = Weapon(damage: 40)
a.fire(b)
var state = State.running
var seen = ""
match state {
    .idle { seen = "fermo" }
    .running, .jumping { seen = "in movimento" }
}
print(a.describe(), b.damage, seen, state == .running, State.all.count)
)LIM");
    INFO(report(run.diagnostics));
    REQUIRE(run.compiled);
    CHECK(run.errors.empty());
    CHECK(run.lines == std::vector<std::string>{"Spada 25 15 in movimento true 3"});
}

TEST_CASE("Opzionali, ?. e ??", "[lim]") {
    auto run = execute(R"LIM(
let players: [Text: Text] = ["p1": "Ada"]
let found = players["p1"]
let missing = players["p9"]
print(found ?? "Unknown", missing ?? "Unknown", missing == none)
if found != none {
    print(found.upper())
}
)LIM");
    INFO(report(run.diagnostics));
    REQUIRE(run.compiled);
    CHECK(run.errors.empty());
    CHECK(run.lines == std::vector<std::string>{"Ada Unknown true", "ADA"});
}

TEST_CASE("Async, await e Task", "[lim][async]") {
    auto run = execute(R"LIM(
async fn load(name: Text) -> Text {
    await Task.wait(0.1)
    return "caricato \(name)"
}
print("prima")
let result = await load("profilo")
print(result)
Task.spawn(fn () { print("in parallelo") })
await Task.nextFrame()
print("dopo un frame")
Task.delay(0.2, fn () { print("ritardato") })
)LIM",
                       20);
    INFO(report(run.diagnostics));
    REQUIRE(run.compiled);
    CHECK(run.errors.empty());
    CHECK(run.lines == std::vector<std::string>{"prima", "caricato profilo", "in parallelo", "dopo un frame", "ritardato"});
}

TEST_CASE("Diagnostici precisi di tipo e sintassi", "[lim][diagnostics]") {
    auto typed = execute("let speed: Number = \"fast\"");
    CHECK_FALSE(typed.compiled);
    REQUIRE(!typed.diagnostics.empty());
    const Diagnostic& d = typed.diagnostics.front();
    CHECK(d.expected == "Number");
    CHECK(d.actual == "Text");
    CHECK(d.range.start.line == 1);
    CHECK(d.range.start.column == 21);
    CHECK(render(d, "let speed: Number = \"fast\"").find("^^^^^^") != std::string::npos);
    CHECK_FALSE(execute("let =").compiled);
    CHECK(hasDiagnostic(execute("let x = 1\nx = 2"), "costante"));
    CHECK(hasDiagnostic(execute("print(undefinedName)"), "Nome sconosciuto"));
    CHECK(hasDiagnostic(execute("let a = 1\nlet a = 2"), "già dichiarato"));
    CHECK(hasDiagnostic(execute("fn f() {\n  await Task.wait(1)\n}"), "async"));
    CHECK(hasDiagnostic(execute("fn f(a: Number, b: Number) { }\nf(1)"), "mancanti"));
    CHECK(hasDiagnostic(execute("fn f(a: Number) { }\nf(1, 2)"), "Troppi"));
    CHECK(hasDiagnostic(execute("break"), "ciclo"));
    CHECK(hasDiagnostic(execute("let t = \"a\" + 1"), "interpolazione"));
    CHECK(hasDiagnostic(execute("let v = Vec3(1, 2, 3)\nprint(v.w)"), "non è un membro"));
}

TEST_CASE("Errori a runtime con file, riga, colonna e stack", "[lim][runtime]") {
    auto run = execute("let a = [1]\nfn boom() {\n    return a[5]\n}\nboom()");
    INFO(report(run.diagnostics));
    REQUIRE(run.compiled);
    REQUIRE(run.errors.size() == 1);
    CHECK(run.errors[0].find("Prova.lim:3:") == 0);
    CHECK(run.errors[0].find("fuori dalla lista") != std::string::npos);
}

TEST_CASE("Il ciclo infinito viene interrotto senza bloccare", "[lim][sandbox]") {
    auto started = std::chrono::steady_clock::now();
    double cpu = support::timing::threadSeconds();
    auto run = execute("while true {\n}\n");
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    cpu = support::timing::threadSeconds() - cpu;
    INFO(report(run.diagnostics));
    REQUIRE(run.compiled);
    REQUIRE(run.errors.size() == 1);
    CHECK(run.errors[0].find("Tempo di esecuzione esaurito") != std::string::npos);
    UNINSTRUMENTED_CHECK(cpu < 2.0);
    TIMED_CHECK(elapsed < 2.0);
    auto recursion = execute("fn f(n: Integer) -> Integer {\n return f(n + 1)\n}\nf(0)");
    REQUIRE(recursion.errors.size() == 1);
    CHECK(recursion.errors[0].find("ricorsione") != std::string::npos);
}

TEST_CASE("Il limite di memoria ferma le allocazioni enormi", "[lim][sandbox]") {
    auto run = execute("var s = \"x\"\nwhile true {\n    s = s + s\n}");
    REQUIRE(run.errors.size() == 1);
    CHECK((run.errors[0].find("Memoria esaurita") != std::string::npos || run.errors[0].find("Tempo") != std::string::npos));
}

TEST_CASE("Nessuna API di sistema esposta agli script", "[lim][sandbox]") {
    for (const char* name : {"File", "Process", "Shell", "SocketRaw", "DynamicLibrary", "os", "io", "require", "loadstring"}) {
        auto run = execute(std::string("print(") + name + ")");
        CHECK_FALSE(run.compiled);
    }
}

TEST_CASE("Bytecode: serializzazione e validazione", "[lim][bytecode]") {
    TypeRegistry registry = standardRegistry();
    AnalyzeOptions options;
    auto result = compileSource("fn f(x: Number) -> Number { return x * 2 }\nvar t = 0\nfor i in 1...3 { t += f(i) }\nprint(t)", "B", registry, options);
    INFO(report(result.diagnostics));
    REQUIRE(result.module);
    std::string bytes = serialize(*result.module);
    auto loaded = deserialize(bytes);
    REQUIRE(loaded.module);
    CHECK(*loaded.module == *result.module);
    CHECK_FALSE(deserialize("").module);
    CHECK_FALSE(deserialize("LIMB").module);
    std::string truncated = bytes.substr(0, bytes.size() / 2);
    CHECK_FALSE(deserialize(truncated).module);
    Module broken = *result.module;
    broken.prototypes[broken.main].code[0].op = Op::getLocal;
    broken.prototypes[broken.main].code[0].a = 9999;
    CHECK(validate(broken).has_value());
    Module jump = *result.module;
    jump.prototypes[jump.main].code.back() = {Op::jump, 100000, 0};
    CHECK(validate(jump).has_value());
    Module underflow = *result.module;
    underflow.prototypes[underflow.main].code.insert(underflow.prototypes[underflow.main].code.begin(), {Op::pop, 0, 0});
    underflow.prototypes[underflow.main].lines.insert(underflow.prototypes[underflow.main].lines.begin(), 1);
    underflow.prototypes[underflow.main].columns.insert(underflow.prototypes[underflow.main].columns.begin(), 1);
    CHECK(validate(underflow).has_value());
}

TEST_CASE("Fuzz: lexer, parser e loader non vanno in crash", "[lim][fuzz]") {
    std::mt19937 random(7);
    const std::string alphabet = "let var fn if else while for in {}()[]\"\\:=+-*/%<>!?.,;0123456789abcXYZ\n\t match class enum on await async none true";
    TypeRegistry registry = standardRegistry();
    AnalyzeOptions options;
    for (int i = 0; i < 400; ++i) {
        std::string source;
        int length = static_cast<int>(random() % 200);
        for (int j = 0; j < length; ++j) source += alphabet[random() % alphabet.size()];
        CHECK_NOTHROW(compileSource(source, "Fuzz", registry, options));
        std::string bytes;
        for (int j = 0; j < length; ++j) bytes += static_cast<char>(random() % 256);
        CHECK_NOTHROW(deserialize("LIMB" + bytes));
    }
    SUCCEED();
}

TEST_CASE("Lim: limiti degli interi", "[lim][sandbox][integer]") {
    auto run = execute(R"LIM(let high = 9223372036854775807
let low = -high - 1
print(low % -1)
print(Math.abs(low) > 0)
var count = 0
for value in (high - 1)...high {
    count += 1
}

print(count)
for value in high..<high {
    count += 1
}
print(count)
)LIM");
    INFO(report(run.diagnostics));
    REQUIRE(run.compiled);
    CHECK(run.errors.empty());
    CHECK(run.lines == std::vector<std::string>{"0", "true", "2", "2"});
}

TEST_CASE("Lim: validazione del bytecode e operandi", "[lim][bytecode][sandbox]") {
    Vm vm;
    CHECK(vm.run(nullptr, vm.newEnvironment("Invalid"), "Invalid")->state == Fiber::State::failed);
    auto empty = std::make_shared<Module>();
    CHECK(vm.run(empty, vm.newEnvironment("Empty"), "Empty")->state == Fiber::State::failed);
    Module invalid;
    invalid.prototypes.resize(1);
    auto& main = invalid.prototypes[0];
    main.maxStack = 2;
    main.code = {{Op::none}, {Op::iterNext, 4, 1}, {Op::pop}, {Op::jump, 1}, {Op::pop}, {Op::returnNone}};
    main.lines.assign(main.code.size(), 1);
    main.columns.assign(main.code.size(), 1);
    REQUIRE_FALSE(validate(invalid));
    auto result = vm.run(std::make_shared<Module>(invalid), vm.newEnvironment("Operand"), "Operand");
    CHECK(result->state == Fiber::State::failed);
    CHECK(result->task->error.find("iteratore richiesto") != std::string::npos);
    main.captures.push_back({true, 0});
    CHECK(validate(invalid));
    auto program = compileSource("print(1)", "Native", standardRegistry(), {});
    REQUIRE(program.module);
    vm.defineBuiltin("print", vm.native("print", [](NativeCall&) -> NativeResult { throw std::runtime_error("native failure"); }));
    auto native = vm.run(std::make_shared<Module>(*program.module), vm.newEnvironment("Native"), "Native");
    CHECK(native->state == Fiber::State::failed);
    CHECK(native->task->error.find("native failure") != std::string::npos);
    Limits limits;
    limits.maximumStack = 1;
    Vm limited(limits);
    CHECK(limited.run(std::make_shared<Module>(*program.module), limited.newEnvironment("Stack"), "Stack")->state == Fiber::State::failed);
}

TEST_CASE("Lim: limite di tempo della VM", "[lim][limits]") {
    AnalyzeOptions options;
    options.extraGlobals.push_back({"pause", Type::function({}, Type::none())});
    auto program = compileSource("for i in 1...8 {\n    pause()\n}\nvar s = 0\nfor i in 1...20000 {\n    s += i\n}\nprint(s)", "Attesa", standardRegistry(), options);
    INFO(report(program.diagnostics));
    REQUIRE(program.module);
    Limits quick;
    quick.sliceSeconds = 0.25;
    Vm vm(quick);
    std::vector<std::string> printed;
    vm.defineBuiltin("pause", vm.native("pause", [](NativeCall&) -> NativeResult {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        return NativeResult::of(Value());
    }));
    vm.defineBuiltin("print", vm.native("print", [&](NativeCall& call) -> NativeResult {
        printed.push_back(vm.toText(call.argument(0)));
        return NativeResult::of(Value());
    }));
    auto started = std::chrono::steady_clock::now();
    auto fiber = vm.run(std::make_shared<Module>(*program.module), vm.newEnvironment("Attesa"), "Attesa");
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count() > quick.sliceSeconds);
    CHECK(fiber->state == Fiber::State::done);
    CHECK(printed == std::vector<std::string>{"200010000"});
    auto endless = compileSource("var n = 0\nwhile true {\n    n += 1\n}", "Infinito", standardRegistry(), {});
    REQUIRE(endless.module);
    Limits limits;
    limits.instructionsPerSlice = std::numeric_limits<std::uint64_t>::max();
    limits.sliceSeconds = 0.25;
    Vm bounded(limits);
    auto spinning = std::chrono::steady_clock::now();
    double spun = support::timing::threadSeconds();
    auto stuck = bounded.run(std::make_shared<Module>(*endless.module), bounded.newEnvironment("Infinito"), "Infinito");
    CHECK(stuck->state == Fiber::State::failed);
    CHECK(stuck->task->error.find("Tempo di esecuzione esaurito") != std::string::npos);
    CHECK(support::timing::threadSeconds() - spun < 2);
    TIMED_CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - spinning).count() < 5);
}

TEST_CASE("Lim: bytecode mutato", "[lim][bytecode][fuzz]") {
    auto program = compileSource("var n = 0\nfor i in 1...3 { n += i }\nprint(n)", "Mutation", standardRegistry(), {});
    REQUIRE(program.module);
    for (std::size_t pc = 0; pc < program.module->prototypes[0].code.size(); ++pc) {
        for (unsigned op = 0; op < static_cast<unsigned>(Op::count); ++op) {
            auto candidate = std::make_shared<Module>(*program.module);
            candidate->prototypes[0].code[pc].op = static_cast<Op>(op);
            Limits limits;
            limits.instructionsPerSlice = 256;
            limits.maximumFrames = 8;
            Vm vm(limits);
            INFO(pc << " " << opName(static_cast<Op>(op)));
            CHECK_NOTHROW(vm.run(candidate, vm.newEnvironment("Mutation"), "Mutation"));
        }
    }
}

namespace {

struct Measured {
    std::vector<std::string> lines, errors;
    std::uint64_t instructions = 0;
    double cpu = 0;
};

Measured measure(const std::string& source, Limits limits = {}) {
    Measured out;
    TypeRegistry registry = standardRegistry();
    auto result = compileSource(source, "Misura", registry, {});
    INFO(report(result.diagnostics));
    REQUIRE(result.module);
    Vm vm(limits);
    vm.setOutput([&](const OutputMessage& m) { (m.severity == Severity::error ? out.errors : out.lines).push_back(m.text); });
    auto module = std::make_shared<const Module>(std::move(*result.module));
    double before = support::timing::threadSeconds();
    vm.run(module, vm.newEnvironment("Misura"), "Misura.lim");
    out.cpu = support::timing::threadSeconds() - before;
    out.instructions = vm.statistics().instructions;
    return out;
}

}

TEST_CASE("Lim: watchdog deterministico", "[lim][sandbox][watchdog]") {
    const std::string million = "var s = 0\nfor i in 1...1000000 {\n    s += i\n}\nprint(s)\n";
    Limits generous;
    if (support::timing::validity() == support::timing::Validity::instrumented) generous.sliceSeconds *= 20;
    auto legit = measure(million, generous);
    INFO((legit.errors.empty() ? std::string() : legit.errors.front()));
    REQUIRE(legit.errors.empty());
    CHECK(legit.lines == std::vector<std::string>{"500000500000"});
    double throughput = static_cast<double>(legit.instructions) / std::max(legit.cpu, 1e-6);
    WARN("ciclo da 1.000.000: " << legit.instructions << " istruzioni in " << legit.cpu * 1000 << " ms di CPU · " << throughput / 1e6 << " milioni di istruzioni al secondo · budget completo stimato " << 20e6 / throughput * 1000 << " ms");
    CHECK(legit.instructions < Limits{}.instructionsPerSlice);

    Limits slowMachine;
    slowMachine.sliceSeconds = Limits{}.sliceSeconds / 6;
    auto slow = measure(million, slowMachine);
    INFO("macchina 6 volte più lenta simulata: " << (slow.errors.empty() ? std::string("nessun errore") : slow.errors.front()));
    UNINSTRUMENTED_CHECK(slow.errors.empty());
    UNINSTRUMENTED_CHECK(slow.lines == std::vector<std::string>{"500000500000"});

    auto endless = measure("var n = 0\nwhile true {\n    n += 1\n}\n");
    REQUIRE(endless.errors.size() == 1);
    CHECK(endless.errors[0].find("Tempo di esecuzione esaurito") != std::string::npos);
    UNINSTRUMENTED_CHECK(endless.instructions == Limits{}.instructionsPerSlice + 1);
    auto again = measure("var n = 0\nwhile true {\n    n += 1\n}\n", slowMachine);
    UNINSTRUMENTED_CHECK(again.instructions == endless.instructions);

    Limits small;
    small.instructionsPerSlice = 200000;
    auto boundary = measure("var s = 0\nfor i in 1...10000 {\n    s += i\n}\nprint(s)\n", small);
    REQUIRE(boundary.errors.empty());
    std::uint64_t needed = boundary.instructions;
    small.instructionsPerSlice = needed;
    CHECK(measure("var s = 0\nfor i in 1...10000 {\n    s += i\n}\nprint(s)\n", small).errors.empty());
    small.instructionsPerSlice = needed - 2;
    CHECK(measure("var s = 0\nfor i in 1...10000 {\n    s += i\n}\nprint(s)\n", small).errors.size() == 1);

    auto recursion = measure("fn f(n: Integer) -> Integer {\n    return f(n + 1)\n}\nprint(f(0))\n");
    REQUIRE(recursion.errors.size() == 1);
    auto yielding = execute("var total = 0\nfor round in 1...30 {\n    for i in 1...200000 {\n        total += 1\n    }\n    await Task.nextFrame()\n}\nprint(total)\n", 40);
    INFO((yielding.errors.empty() ? std::string() : yielding.errors.front()));
    CHECK(yielding.errors.empty());
    CHECK(yielding.lines == std::vector<std::string>{"6000000"});
}
