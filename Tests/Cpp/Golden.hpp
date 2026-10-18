#pragma once

#include "Engine/Core/Json.hpp"
#include "Engine/Core/Math.hpp"
#include "Engine/DataModel/ClassRegistry.hpp"

#include <Catch2/catch_amalgamated.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace golden {

inline std::filesystem::path root() {
    const char* value = std::getenv("LUDISSEY_ROOT");
    return value ? std::filesystem::path(value) : std::filesystem::current_path();
}

inline std::string readText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    std::stringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

inline ld::Json load(const std::string& name) {
    auto parsed = ld::Json::parse(readText(root() / "Tests/Golden" / (name + ".json")), 4096);
    REQUIRE(parsed.ok());
    return *parsed;
}

inline bool writing() {
    const char* mode = std::getenv("LUDISSEY_GOLDEN");
    return mode && std::string(mode) == "write";
}

inline void save(const std::string& name, const ld::Json& value) { std::ofstream(root() / "Tests/Golden" / (name + ".json"), std::ios::binary) << value.dump(true) << "\n"; }

inline const ld::ClassRegistry& registry() {
    if (!ld::ClassRegistry::hasShared()) {
        auto loaded = ld::ClassRegistry::load(root() / "HistoricalAPI/2017.json", root() / "Resources/class-rules.json");
        REQUIRE(loaded.ok());
        ld::ClassRegistry::setShared(*loaded);
    }
    return ld::ClassRegistry::shared();
}

inline bool close(double a, double b, double tolerance) {
    if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
    return std::fabs(a - b) <= tolerance * std::max(1.0, std::max(std::fabs(a), std::fabs(b)));
}

struct Options {
    double tolerance = 1e-9;
    bool bridgedBooleans = false;
};

inline void compare(const ld::Json& actual, const ld::Json& expected, const std::string& path, const Options& options, std::vector<std::string>& differences) {
    double tolerance = options.tolerance;
    if (differences.size() > 12) return;
    if (options.bridgedBooleans && actual.isNumber() && expected.isBool() && (actual.number() == 0 || actual.number() == 1)) {
        if ((actual.number() == 1) != expected.boolean()) differences.push_back(path + ": " + actual.dump() + " invece di " + expected.dump());
        return;
    }
    if (actual.isNumber() && expected.isNumber()) {
        if (!close(actual.number(), expected.number(), tolerance)) differences.push_back(path + ": " + ld::formatNumber(actual.number()) + " invece di " + ld::formatNumber(expected.number()));
        return;
    }
    if (actual.type() != expected.type()) {
        differences.push_back(path + ": " + actual.dump() + " invece di " + expected.dump());
        return;
    }
    if (actual.isArray()) {
        if (actual.size() != expected.size()) {
            differences.push_back(path + ": " + std::to_string(actual.size()) + " elementi invece di " + std::to_string(expected.size()));
            return;
        }
        for (std::size_t i = 0; i < actual.size(); ++i) compare(actual[i], expected[i], path + "[" + std::to_string(i) + "]", options, differences);
        return;
    }
    if (actual.isObject()) {
        for (const auto& [key, value] : expected.members()) {
            const ld::Json* other = actual.find(key);
            if (!other) differences.push_back(path + "." + key + ": mancante");
            else compare(*other, value, path + "." + key, options, differences);
        }
        for (const auto& [key, value] : actual.members())
            if (!expected.find(key)) differences.push_back(path + "." + key + ": in più (" + value.dump() + ")");
        return;
    }
    if (actual != expected) differences.push_back(path + ": " + actual.dump() + " invece di " + expected.dump());
}

inline std::string same(const ld::Json& actual, const ld::Json& expected, Options options) {
    std::vector<std::string> differences;
    compare(actual, expected, "$", options, differences);
    std::string text;
    for (const auto& d : differences) text += d + "\n";
    return text;
}

inline std::string same(const ld::Json& actual, const ld::Json& expected, double tolerance = 1e-9) { return same(actual, expected, Options{tolerance, false}); }

inline ld::Vec3 vec3(const ld::Json& value) { return {value[0].number(), value[1].number(), value[2].number()}; }
inline ld::Json array(ld::Vec3 v) { return ld::Json::array({v.x, v.y, v.z}); }

}
