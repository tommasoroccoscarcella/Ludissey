#include "Engine/Audio/Audio.hpp"

#include <algorithm>
#include <atomic>

namespace ld::audio {

namespace {

std::shared_ptr<Output>& current() {
    static std::shared_ptr<Output> value;
    return value;
}

std::atomic<double>& master() {
    static std::atomic<double> value{1.0};
    return value;
}

}

void setOutput(std::shared_ptr<Output> output) {
    current() = std::move(output);
    if (current()) current()->setMaster(master().load());
}

std::shared_ptr<Output> output() { return current(); }

double masterVolume() { return master().load(); }

void setMasterVolume(double volume) {
    master() = std::clamp(volume, 0.0, 1.0);
    if (current()) current()->setMaster(master().load());
}

}
