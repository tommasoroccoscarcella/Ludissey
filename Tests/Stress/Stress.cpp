#include "Stress.hpp"
#include "Tests/Support/Sanitizers.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <mach/mach.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>

namespace stress {

namespace {

ld::Json& report() {
    static ld::Json value = ld::Json::object();
    return value;
}

std::mutex& guard() {
    static std::mutex value;
    return value;
}

double started = now();
int comparisons = 0;

ld::Json& measured() {
    static ld::Json value = ld::Json::object();
    return value;
}

std::filesystem::path baselineFile() { return root() / "Tests/Stress/Baseline.json"; }

bool writing() {
    const char* mode = std::getenv("LUDISSEY_STRESS_BASELINE");
    return mode && std::string(mode) == "write";
}

const ld::Json& baseline() {
    static ld::Json value = [] {
        std::ifstream stream(baselineFile());
        std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        auto parsed = ld::Json::parse(text);
        return parsed ? *parsed : ld::Json::object();
    }();
    return value;
}

class Listener final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testCaseStarting(const Catch::TestCaseInfo& info) override {
        settle();
        current = info.name;
        caseStarted = now();
        pressure = thermal();
    }

    void testCaseEnded(const Catch::TestCaseStats& stats) override {
        record("cases", current, ld::Json::object({{"seconds", now() - caseStarted}, {"passed", stats.totals.assertions.failed == 0}, {"megabytes", megabytes()}, {"threads", threads()}, {"pressione termica", static_cast<double>(std::max(pressure, thermal()))}, {"carico del sistema", load()}}));
        hottest = std::max({hottest, pressure, thermal()});
        busiest = std::max(busiest, load());
    }

    void testRunEnded(const Catch::TestRunStats& stats) override {
        std::lock_guard<std::mutex> lock(guard());
        ld::Json summary = ld::Json::object({{"profile", profileName()}, {"duration", now() - started}, {"cases", static_cast<double>(stats.totals.testCases.total())},
                                             {"failed", static_cast<double>(stats.totals.testCases.failed)}, {"peakMegabytes", peak()},
                                             {"pressione termica massima", static_cast<double>(hottest)}, {"carico massimo del sistema", busiest}, {"core", static_cast<double>(cores())}});
        if (hottest >= 2) std::cout << "Pressione termica " << hottest << " durante la corsa: macOS ha limitato la CPU e le misure di tempo non sono valide.\n";
        if (busiest >= cores()) std::cout << "Carico del sistema " << busiest << " su " << cores() << " core durante la corsa: altri programmi hanno conteso la CPU e le misure di tempo non sono valide.\n";
        report().set("summary", summary);
        report().set("baseline", ld::Json::object({{"macchina", machine()}, {"misure confrontate", comparisons}, {"confronto", !timed() ? "saltato: build con sanitizer" : !baseline()["machine"].isString() ? "saltato: baseline assente" : !(baseline()["machine"] == ld::Json(machine())) ? "saltato: baseline di un'altra macchina" : comparisons > 0 ? "eseguito sulle misure valide" : "non eseguito: nessuna misura confrontabile"}}));
        if (writing() && stats.totals.testCases.failed == 0) std::ofstream(baselineFile()) << ld::Json::object({{"machine", machine()}, {"profile", profileName()}, {"metrics", measured()}}).dump(true) << "\n";
        std::error_code problem;
        auto folder = std::filesystem::temp_directory_path() / "LudisseyStress";
        std::filesystem::create_directories(folder, problem);
        auto file = folder / (profileName() + ".json");
        std::ofstream(file) << report().dump() << "\n";
        std::cout << "\nReport di stress: " << file.string() << "\n" << report().dump(true) << "\n";
    }

private:
    std::string current;
    double caseStarted = 0;
    int pressure = 0;
    int hottest = 0;
    double busiest = 0;
};

CATCH_REGISTER_LISTENER(Listener)

}

Profile profile() {
    const char* value = std::getenv("LUDISSEY_STRESS_PROFILE");
    std::string name = value ? value : "quick";
    if (name == "extended") return Profile::extended;
    if (name == "standard") return Profile::standard;
    return Profile::quick;
}

std::string profileName() {
    switch (profile()) {
    case Profile::quick: return "quick";
    case Profile::standard: return "standard";
    case Profile::extended: return "extended";
    }
    return "quick";
}

int scaled(int quick, int standard, int extended) {
    int base = profile() == Profile::extended ? extended : profile() == Profile::standard ? standard : quick;
    const char* factor = std::getenv("LUDISSEY_STRESS_SCALE");
    double scale = factor ? std::atof(factor) : 1;
    return std::max(1, static_cast<int>(base * (scale > 0 ? scale : 1)));
}

double seconds(double quick, double standard, double extended) {
    double base = profile() == Profile::extended ? extended : profile() == Profile::standard ? standard : quick;
    const char* factor = std::getenv("LUDISSEY_STRESS_SCALE");
    double scale = factor ? std::atof(factor) : 1;
    return base * (scale > 0 ? scale : 1);
}

std::atomic<double>& highest() {
    static std::atomic<double> value{0};
    return value;
}

double megabytes() {
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return 0;
    double value = static_cast<double>(info.phys_footprint) / 1048576.0;
    double seen = highest().load();
    while (value > seen && !highest().compare_exchange_weak(seen, value)) {}
    return value;
}

double graphicsMegabytes() {
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS || count < TASK_VM_INFO_REV6_COUNT) return 0;
    return static_cast<double>(info.ledger_tag_graphics_footprint + info.ledger_tag_graphics_footprint_compressed) / 1048576.0;
}

double peak() {
    megabytes();
    return highest().load();
}

int cores() { return support::timing::cores(); }

bool quiet() { return thermal() < 2 && load() < cores() * 0.75; }

double waited = 0;

void settle() {
    if (!timed() || support::timing::lowPower() || support::timing::validity() == support::timing::Validity::unavailable || waited >= 600) return;
    double begun = now(), calm = 0;
    bool announced = false;
    while (now() - begun < 600 - waited && calm < 30) {
        if (quiet()) calm += 5;
        else {
            calm = 0;
            if (!announced) std::cout << "Macchina occupata (pressione termica " << thermal() << ", carico " << load() << " su " << cores() << " core): attendo prima delle misure.\n" << std::flush;
            announced = true;
        }
        if (!announced) return;
        if (calm < 30) sleep(5);
    }
    waited += now() - begun;
    record("condizioni", "attesa in secondi", waited);
}

double load() { return support::timing::load(); }

int thermal() { return support::timing::thermal(); }

int threads() {
    thread_act_array_t list = nullptr;
    mach_msg_type_number_t count = 0;
    if (task_threads(mach_task_self(), &list, &count) != KERN_SUCCESS) return 0;
    for (mach_msg_type_number_t i = 0; i < count; ++i) mach_port_deallocate(mach_task_self(), list[i]);
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(list), count * sizeof(thread_act_t));
    return static_cast<int>(count);
}

double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::filesystem::path root() {
    const char* value = std::getenv("LUDISSEY_ROOT");
    return value ? std::filesystem::path(value) : std::filesystem::current_path();
}

double Distribution::percentile(double fraction) const {
    if (samples.empty()) return 0;
    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());
    std::size_t index = std::min(sorted.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(sorted.size())));
    return sorted[index];
}

double Distribution::worst() const { return samples.empty() ? 0 : *std::max_element(samples.begin(), samples.end()); }

std::size_t Distribution::above(double limit) const {
    return static_cast<std::size_t>(std::count_if(samples.begin(), samples.end(), [limit](double v) { return v > limit; }));
}

Watchdog::Watchdog(std::string title, double seconds) : name(std::move(title)), limit(seconds), last(now()) {
    thread = std::thread([this] {
        while (!finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (finished) return;
            if (now() - last > limit) {
                std::string message = "\nBLOCCO: «" + name + "» non risponde da " + std::to_string(static_cast<int>(limit)) + " s\n";
                ssize_t written = ::write(2, message.data(), message.size());
                static_cast<void>(written);
                std::abort();
            }
        }
    });
}

Watchdog::~Watchdog() {
    finished = true;
    thread.join();
}

void Watchdog::beat() { last = now(); }

bool timed() { return !support::instrumented; }

std::string machine() {
    char brand[256] = {0}, model[128] = {0};
    std::size_t brandSize = sizeof brand, modelSize = sizeof model;
    sysctlbyname("machdep.cpu.brand_string", brand, &brandSize, nullptr, 0);
    sysctlbyname("hw.model", model, &modelSize, nullptr, 0);
    return std::string(model) + ", " + brand;
}

bool regressed(const std::string& metric, double value, double factor, double slack) {
    std::lock_guard<std::mutex> lock(guard());
    measured().set(metric, value);
    if (writing() || !timed() || !(baseline()["machine"] == ld::Json(machine())) || !baseline()["metrics"][metric].isNumber()) return false;
    ++comparisons;
    return value > baseline()["metrics"][metric].number() * factor + slack;
}

void record(const std::string& section, const std::string& key, const ld::Json& value) {
    std::lock_guard<std::mutex> lock(guard());
    if (!report().contains(section)) report().set(section, ld::Json::object());
    report()[section].set(key, value);
}

void record(const std::string& section, const std::string& key, const Distribution& frames) {
    record(section, key, ld::Json::object({{"samples", static_cast<double>(frames.count())}, {"p50", frames.percentile(0.5) * 1000}, {"p90", frames.percentile(0.9) * 1000}, {"p95", frames.percentile(0.95) * 1000}, {"p99", frames.percentile(0.99) * 1000}, {"worst", frames.worst() * 1000}}));
}

}
