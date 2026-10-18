#pragma once

#include "Engine/Rendering/Renderer.hpp"

#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace ld::render {

struct GraphicsLevel {
    double scale;
    int samples;
    bool shadows;
    bool deferredShadows;
    bool waterDepth;
    std::string title;
    double pixelSamples() const { return scale * scale * samples; }
    static const std::vector<GraphicsLevel>& ladder();
};

class GraphicsQuality {
public:
    static constexpr const char* storageKey = "LudisseyGraphicsLevel";
    static constexpr std::size_t window = 90;
    static constexpr double budget = 1.0 / 60;
    static constexpr double integratedSamples = 4500000;
    static constexpr double discreteSamples = 16000000;

    struct Storage {
        std::function<std::optional<int>()> load;
        std::function<void(int)> save;
    };
    static Storage preferences();

    explicit GraphicsQuality(bool lowPower = Renderer::shared().lowPower(), Storage storage = preferences());
    int level() const { return current; }
    bool automatic = true;
    const double sampleBudget;
    void fit(double width, double height);
    void restart(double now);
    void record(double time);
    double percentile(double fraction) const;
    bool evaluate(double now);
    void choose(std::optional<int> value);
    Quality settings(double width, double height);

private:
    Storage storage;
    mutable std::mutex lock;
    std::vector<double> intervals;
    std::optional<double> lastFrame;
    double started = 0;
    int strikes = 0;
    int current = 0;
};

}
