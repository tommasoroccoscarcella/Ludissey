#pragma once

#include "Engine/Core/Json.hpp"
#include "Tests/Support/Timing.hpp"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace stress {

enum class Profile { quick, standard, extended };

Profile profile();
std::string profileName();
int scaled(int quick, int standard, int extended);
double seconds(double quick, double standard, double extended);
double megabytes();
double graphicsMegabytes();
int threads();
double now();
std::filesystem::path root();

struct Distribution {
    std::vector<double> samples;
    void add(double value) { samples.push_back(value); }
    double percentile(double fraction) const;
    double worst() const;
    std::size_t count() const { return samples.size(); }
    std::size_t above(double limit) const;
};

class Watchdog {
public:
    Watchdog(std::string name, double limit);
    ~Watchdog();
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;
    void beat();

private:
    std::string name;
    double limit;
    std::atomic<double> last;
    std::atomic<bool> finished{false};
    std::thread thread;
};

double peak();
int thermal();
double load();
int cores();
bool quiet();
void settle();
bool timed();
std::string machine();
bool regressed(const std::string& metric, double value, double factor = 1.75, double slack = 0.002);
void record(const std::string& section, const std::string& key, const ld::Json& value);
void record(const std::string& section, const std::string& key, const Distribution& frames);

}
