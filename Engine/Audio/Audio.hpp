#pragma once

#include <filesystem>
#include <memory>
#include <string>

namespace ld::audio {

struct Playback {
    std::filesystem::path file;
    double volume = 0.5;
    double speed = 1;
    bool looped = false;
    bool operator==(const Playback&) const = default;
};

class Output {
public:
    virtual ~Output() = default;
    virtual bool play(const std::string& id, const Playback& playback) = 0;
    virtual void update(const std::string& id, const Playback& playback) = 0;
    virtual bool playing(const std::string& id) = 0;
    virtual void stop(const std::string& id) = 0;
    virtual void setMaster(double volume) {}
};

void setOutput(std::shared_ptr<Output> output);
std::shared_ptr<Output> output();
double masterVolume();
void setMasterVolume(double volume);

}
