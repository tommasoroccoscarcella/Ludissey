#include "Quality.hpp"

#include "Localization/Localization.hpp"

#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>

namespace ld::render {

const std::vector<GraphicsLevel>& GraphicsLevel::ladder() {
    static const std::vector<GraphicsLevel> levels = {
        {2, 4, true, true, true, std::string(localization::localizable("Massima"))},
        {2, 2, true, false, true, std::string(localization::localizable("Alta"))},
        {1.5, 2, true, false, true, std::string(localization::localizable("Media"))},
        {1.25, 1, true, false, false, std::string(localization::localizable("Bassa"))},
        {1, 1, false, false, false, std::string(localization::localizable("Minima"))},
    };
    return levels;
}

GraphicsQuality::Storage GraphicsQuality::preferences() {
    Storage s;
    s.load = [] {
        std::optional<int> result;
        CFPropertyListRef value = CFPreferencesCopyAppValue(CFSTR("LudisseyGraphicsLevel"), kCFPreferencesCurrentApplication);
        if (!value) return result;
        int number = 0;
        if (CFGetTypeID(value) == CFNumberGetTypeID() && CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberIntType, &number)) result = number;
        CFRelease(value);
        return result;
    };
    s.save = [](int level) {
        CFNumberRef number = CFNumberCreate(nullptr, kCFNumberIntType, &level);
        CFPreferencesSetAppValue(CFSTR("LudisseyGraphicsLevel"), number, kCFPreferencesCurrentApplication);
        CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
        CFRelease(number);
    };
    return s;
}

GraphicsQuality::GraphicsQuality(bool lowPower, Storage store) : sampleBudget(lowPower ? integratedSamples : discreteSamples), storage(std::move(store)) {
    int top = static_cast<int>(GraphicsLevel::ladder().size()) - 1;
    auto stored = storage.load ? storage.load() : std::nullopt;
    current = stored ? std::clamp(*stored, 0, top) : (lowPower ? 2 : 0);
}

void GraphicsQuality::fit(double width, double height) {
    if (!automatic || width <= 0 || height <= 0) return;
    const auto& levels = GraphicsLevel::ladder();
    while (current < static_cast<int>(levels.size()) - 1 && width * height * levels[static_cast<std::size_t>(current)].pixelSamples() > sampleBudget) ++current;
}

void GraphicsQuality::restart(double now) {
    std::lock_guard<std::mutex> guard(lock);
    intervals.clear();
    lastFrame.reset();
    started = now;
    strikes = 0;
}

void GraphicsQuality::record(double time) {
    std::lock_guard<std::mutex> guard(lock);
    if (lastFrame) intervals.push_back(time - *lastFrame);
    lastFrame = time;
    if (intervals.size() > window) intervals.erase(intervals.begin(), intervals.begin() + static_cast<std::ptrdiff_t>(intervals.size() - window));
}

double GraphicsQuality::percentile(double fraction) const {
    std::lock_guard<std::mutex> guard(lock);
    auto sorted = intervals;
    std::sort(sorted.begin(), sorted.end());
    if (sorted.empty()) return 0;
    return sorted[std::min(sorted.size() - 1, static_cast<std::size_t>(fraction * static_cast<double>(sorted.size())))];
}

bool GraphicsQuality::evaluate(double now) {
    std::vector<double> sorted;
    {
        std::lock_guard<std::mutex> guard(lock);
        if (intervals.size() < window) return false;
        sorted = intervals;
    }
    std::sort(sorted.begin(), sorted.end());
    if (!automatic || current >= static_cast<int>(GraphicsLevel::ladder().size()) - 1 || now - started <= 3) return false;
    bool slow = sorted[static_cast<std::size_t>(0.9 * static_cast<double>(sorted.size()))] > budget * 1.25;
    bool sustained;
    {
        std::lock_guard<std::mutex> guard(lock);
        intervals.clear();
        lastFrame.reset();
        strikes = slow ? strikes + 1 : 0;
        sustained = strikes >= 2;
    }
    if (!sustained) return false;
    ++current;
    if (storage.save) storage.save(current);
    std::lock_guard<std::mutex> guard(lock);
    strikes = 0;
    started = now;
    return true;
}

void GraphicsQuality::choose(std::optional<int> value) {
    automatic = !value;
    if (value) current = std::clamp(*value, 0, static_cast<int>(GraphicsLevel::ladder().size()) - 1);
    if (storage.save) storage.save(current);
    std::lock_guard<std::mutex> guard(lock);
    intervals.clear();
    lastFrame.reset();
}

Quality GraphicsQuality::settings(double width, double height) {
    fit(width, height);
    const auto& level = GraphicsLevel::ladder()[static_cast<std::size_t>(current)];
    Quality q;
    q.samples = level.samples;
    q.shadows = level.shadows;
    q.deferredShadows = level.deferredShadows;
    q.scale = level.scale;
    q.waterDepth = level.waterDepth;
    return q;
}

}
