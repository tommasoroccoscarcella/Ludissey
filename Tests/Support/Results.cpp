#include "Engine/Core/Json.hpp"
#include "Tests/Support/Sanitizers.hpp"
#include "Tests/Support/Timing.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <crt_externs.h>
#include <notify.h>
#include <sys/sysctl.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace support::timing {

namespace {

struct Counters {
    int executed = 0, passed = 0, failed = 0;
    std::map<std::string, int> skipped;
    std::vector<ld::Json> skips;
    std::vector<ld::Json> failures;
};

Counters& counters() {
    static Counters value;
    return value;
}

std::mutex& guard() {
    static std::mutex value;
    return value;
}

std::string currentCase;
bool powerAtCaseStart = false;

ld::Json where(const char* expression, const char* file, int line) {
    return ld::Json::object({{"test", currentCase}, {"espressione", std::string(expression)}, {"file", std::filesystem::path(file).filename().string()}, {"riga", static_cast<double>(line)}, {"risparmio energetico", lowPower()}, {"risparmio energetico iniziale del test", powerAtCaseStart}, {"carico", load()}, {"pressione termica", static_cast<double>(thermal())}});
}

}

double load() {
    double value[1] = {0};
    return getloadavg(value, 1) == 1 ? value[0] : -1;
}

int thermal() {
    static int token = [] {
        int value = -1;
        if (notify_register_check("com.apple.system.thermalpressurelevel", &value) != NOTIFY_STATUS_OK) value = -1;
        return value;
    }();
    std::uint64_t state = 0;
    if (token < 0 || notify_get_state(token, &state) != NOTIFY_STATUS_OK) return -1;
    return static_cast<int>(state);
}

int cores() {
    int value = 1;
    std::size_t size = sizeof value;
    if (sysctlbyname("hw.logicalcpu", &value, &size, nullptr, 0) != 0 || value < 1) value = 0;
    return value;
}

Validity validity(bool resource) {
    return classify(resource, instrumented, powerAtCaseStart || lowPower(), thermal(), load(), cores());
}

std::string describe(Validity reason) {
    switch (reason) {
    case Validity::valid: return "valida";
    case Validity::instrumented: return "build con sanitizer";
    case Validity::unavailable: return "condizioni del sistema non disponibili";
    case Validity::powerLimited: return "risparmio energetico attivo";
    case Validity::throttled: return "pressione termica " + std::to_string(thermal());
    case Validity::loaded: return "carico " + std::to_string(load()).substr(0, 4) + " su " + std::to_string(cores()) + " core";
    }
    return "sconosciuta";
}

bool required() {
    const char* mode = std::getenv("LUDISSEY_TIMING");
    return mode && std::string(mode) == "require";
}

bool forced() {
    const char* mode = std::getenv("LUDISSEY_TIMING");
    return mode && std::string(mode) == "force";
}

void executed(bool passed, const char* expression, const char* file, int line) {
    std::lock_guard<std::mutex> lock(guard());
    auto& c = counters();
    ++c.executed;
    if (passed) ++c.passed;
    else {
        ++c.failed;
        c.failures.push_back(where(expression, file, line));
    }
}

void skipped(Validity reason, const char* expression, const char* file, int line) {
    std::lock_guard<std::mutex> lock(guard());
    auto& c = counters();
    std::string key = reason == Validity::instrumented ? "sanitizer" : reason == Validity::unavailable ? "ambiente non disponibile" : reason == Validity::powerLimited ? "risparmio energetico" : reason == Validity::throttled ? "pressione termica" : "carico";
    ++c.skipped[key];
    ld::Json entry = where(expression, file, line);
    entry.set("motivo", describe(reason));
    c.skips.push_back(entry);
}

}

namespace {

std::string stamp(bool compact) {
    std::time_t t = std::time(nullptr);
    std::tm local{};
    localtime_r(&t, &local);
    char text[32];
    std::strftime(text, sizeof text, compact ? "%Y%m%d-%H%M%S" : "%Y-%m-%dT%H:%M:%S%z", &local);
    return text;
}

double now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string tail(const std::string& text) { return text.size() > 20000 ? text.substr(text.size() - 20000) : text; }

std::filesystem::path base() {
    if (const char* chosen = std::getenv("LUDISSEY_RESULTS")) return chosen;
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : "/tmp") / "Library/Caches/Ludissey/TestResults";
}

void prune(const std::filesystem::path& folder) {
    std::error_code problem;
    std::vector<std::filesystem::path> runs;
    for (const auto& entry : std::filesystem::directory_iterator(folder, problem))
        if (entry.is_directory()) runs.push_back(entry.path());
    std::sort(runs.begin(), runs.end(), [](const auto& a, const auto& b) { return std::filesystem::last_write_time(a) > std::filesystem::last_write_time(b); });
    for (std::size_t i = 60; i < runs.size(); ++i) {
        auto failureFile = runs[i] / "failures.jsonl";
        if (!std::filesystem::exists(runs[i] / "summary.json") || !std::filesystem::exists(failureFile) || std::filesystem::file_size(failureFile, problem) != 0) continue;
        std::filesystem::remove_all(runs[i], problem);
    }
}

class Results final : public Catch::EventListenerBase {
public:
    explicit Results(const Catch::IConfig* config) : Catch::EventListenerBase(config) { m_preferences.shouldRedirectStdOut = true; }

    void testRunStarting(const Catch::TestRunInfo& info) override {
        name = std::string(info.name);
        std::error_code problem;
        std::filesystem::create_directories(base(), problem);
        prune(base());
        folder = base() / (name + "-" + stamp(true) + "-" + std::to_string(getpid()));
        std::filesystem::create_directories(folder, problem);
        started = now();
        ld::Json arguments = ld::Json::array();
        for (int i = 0; i < *_NSGetArgc(); ++i) arguments.push((*_NSGetArgv())[i]);
        const char* profile = std::getenv("LUDISSEY_STRESS_PROFILE");
        run = ld::Json::object({{"eseguibile", name}, {"inizio", stamp(false)}, {"seed", static_cast<double>(Catch::getSeed())}, {"argomenti", arguments}, {"profilo di stress", profile ? profile : "quick"}, {"core", static_cast<double>(support::timing::cores())}, {"carico iniziale", support::timing::load()}, {"pressione termica iniziale", static_cast<double>(support::timing::thermal())}, {"sanitizer", support::instrumented}, {"modalità delle misure", support::timing::forced() ? "force" : support::timing::required() ? "require" : "standard"}});
        run.set("risparmio energetico iniziale", support::timing::lowPower());
        std::ofstream(folder / "failures.jsonl", std::ios::trunc);
        std::ofstream(folder / "cases.jsonl", std::ios::trunc);
    }

    void testCaseStarting(const Catch::TestCaseInfo& info) override {
        support::timing::currentCase = info.name;
        support::timing::powerAtCaseStart = support::timing::lowPower();
        caseStarted = now();
        sections.clear();
        busiest = std::max(busiest, support::timing::load());
        hottest = std::max(hottest, support::timing::thermal());
    }

    void sectionStarting(const Catch::SectionInfo& info) override { sections.push_back(info.name); }

    void sectionEnded(const Catch::SectionStats&) override {
        if (!sections.empty()) sections.pop_back();
    }

    void assertionEnded(const Catch::AssertionStats& stats) override {
        const auto& result = stats.assertionResult;
        if (result.isOk()) return;
        ld::Json messages = ld::Json::array();
        for (const auto& m : stats.infoMessages) messages.push(m.message);
        auto source = result.getSourceInfo();
        append("failures.jsonl", ld::Json::object({{"tipo", "asserzione"}, {"ora", stamp(false)}, {"test", support::timing::currentCase}, {"sezione", path()}, {"file", std::string(source.file)}, {"riga", static_cast<double>(source.line)}, {"macro", result.getExpressionInMacro()}, {"valori", result.hasExpandedExpression() ? result.getExpandedExpression() : std::string()}, {"messaggio", std::string(result.getMessage())}, {"messaggi", messages}, {"secondi dall'inizio del test", now() - caseStarted}, {"risparmio energetico", support::timing::lowPower()}, {"carico", support::timing::load()}, {"pressione termica", static_cast<double>(support::timing::thermal())}}));
    }

    void fatalErrorEncountered(Catch::StringRef error) override {
        interruptedByFatalError = true;
        append("failures.jsonl", ld::Json::object({{"tipo", "errore fatale"}, {"ora", stamp(false)}, {"test", support::timing::currentCase}, {"sezione", path()}, {"errore", std::string(error)}, {"secondi dall'inizio del test", now() - caseStarted}, {"risparmio energetico", support::timing::lowPower()}, {"carico", support::timing::load()}, {"pressione termica", static_cast<double>(support::timing::thermal())}}));
    }

    void testCaseEnded(const Catch::TestCaseStats& stats) override {
        bool failed = stats.totals.assertions.failed > 0;
        double elapsed = now() - caseStarted;
        busiest = std::max(busiest, support::timing::load());
        hottest = std::max(hottest, support::timing::thermal());
        append("cases.jsonl", ld::Json::object({{"test", stats.testInfo->name}, {"esito", failed ? "fallito" : "superato"}, {"secondi", elapsed}, {"asserzioni", static_cast<double>(stats.totals.assertions.total())}, {"fallite", static_cast<double>(stats.totals.assertions.failed)}, {"risparmio energetico iniziale", support::timing::powerAtCaseStart}, {"risparmio energetico finale", support::timing::lowPower()}}));
        if (failed) append("failures.jsonl", ld::Json::object({{"tipo", "test fallito"}, {"ora", stamp(false)}, {"test", stats.testInfo->name}, {"secondi", elapsed}, {"asserzioni fallite", static_cast<double>(stats.totals.assertions.failed)}, {"stdout", tail(stats.stdOut)}, {"stderr", tail(stats.stdErr)}}));
    }

    void testRunEnded(const Catch::TestRunStats& stats) override {
        const auto& c = support::timing::counters();
        ld::Json skipped = ld::Json::object();
        int skippedTotal = 0;
        for (const auto& [reason, count] : c.skipped) {
            skipped.set(reason, static_cast<double>(count));
            skippedTotal += count;
        }
        ld::Json failures = ld::Json::array(), skips = ld::Json::array();
        for (const auto& f : c.failures) failures.push(f);
        for (const auto& s : c.skips) skips.push(s);
        std::uint64_t failedAssertions = stats.totals.assertions.failed;
        run.set("fine", stamp(false));
        run.set("secondi", now() - started);
        run.set("test", ld::Json::object({{"totali", static_cast<double>(stats.totals.testCases.total())}, {"superati", static_cast<double>(stats.totals.testCases.passed)}, {"falliti", static_cast<double>(stats.totals.testCases.failed)}, {"saltati", static_cast<double>(stats.totals.testCases.skipped)}}));
        run.set("asserzioni", ld::Json::object({{"totali", static_cast<double>(stats.totals.assertions.total())}, {"fallite", static_cast<double>(failedAssertions)}}));
        run.set("misure", ld::Json::object({{"eseguite", static_cast<double>(c.executed)}, {"superate", static_cast<double>(c.passed)}, {"fallite", static_cast<double>(c.failed)}, {"non valide", static_cast<double>(skippedTotal)}, {"motivi", skipped}, {"elenco fallite", failures}, {"elenco non valide", skips}}));
        run.set("carico massimo", busiest);
        run.set("pressione termica massima", static_cast<double>(hottest));
        run.set("risparmio energetico finale", support::timing::lowPower());
        run.set("interrotto", stats.aborting || interruptedByFatalError);
        run.set("codice di uscita atteso", static_cast<double>(std::min<std::uint64_t>(failedAssertions, 255)));
        std::ofstream(folder / "summary.json") << run.dump(true) << "\n";
        std::cout << "\nMisure di prestazione: eseguite " << c.executed << " (superate " << c.passed << ", fallite " << c.failed << "), non valide " << skippedTotal;
        for (const auto& [reason, count] : c.skipped) std::cout << " · " << reason << " " << count;
        std::cout << "\nRisultati conservati in " << folder.string() << "\n" << std::flush;
    }

private:
    std::string name;
    std::filesystem::path folder;
    ld::Json run = ld::Json::object();
    double started = 0, caseStarted = 0, busiest = 0;
    int hottest = 0;
    bool interruptedByFatalError = false;
    std::vector<std::string> sections;

    std::string path() const {
        std::string text;
        for (std::size_t i = 1; i < sections.size(); ++i) text += (text.empty() ? "" : " / ") + sections[i];
        return text;
    }

    void append(const char* file, const ld::Json& value) const {
        std::ofstream stream(folder / file, std::ios::app);
        stream << value.dump() << "\n";
        stream.flush();
    }
};

}

CATCH_REGISTER_LISTENER(Results)
