#include "LocalPreferences.hpp"

#include <CoreFoundation/CoreFoundation.h>

#include <map>
#include <mutex>

namespace ld::local_preferences {

namespace {

std::mutex& guard() {
    static std::mutex value;
    return value;
}

bool& inMemory() {
    static bool value = false;
    return value;
}

std::map<std::string, double>& memory() {
    static std::map<std::string, double> value;
    return value;
}

CFStringRef name(const std::string& key) { return CFStringCreateWithCString(nullptr, key.c_str(), kCFStringEncodingUTF8); }

}

std::optional<double> number(const std::string& key) {
    std::lock_guard<std::mutex> lock(guard());
    if (inMemory()) {
        auto found = memory().find(key);
        return found == memory().end() ? std::nullopt : std::optional<double>(found->second);
    }
    CFStringRef k = name(key);
    CFPropertyListRef value = CFPreferencesCopyAppValue(k, kCFPreferencesCurrentApplication);
    CFRelease(k);
    std::optional<double> result;
    double number = 0;
    if (value && CFGetTypeID(value) == CFNumberGetTypeID() && CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberDoubleType, &number)) result = number;
    if (value) CFRelease(value);
    return result;
}

void setNumber(const std::string& key, double value) {
    std::lock_guard<std::mutex> lock(guard());
    if (inMemory()) {
        memory()[key] = value;
        return;
    }
    CFStringRef k = name(key);
    CFNumberRef number = CFNumberCreate(nullptr, kCFNumberDoubleType, &value);
    CFPreferencesSetAppValue(k, number, kCFPreferencesCurrentApplication);
    CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
    CFRelease(number);
    CFRelease(k);
}

void remove(const std::string& key) {
    std::lock_guard<std::mutex> lock(guard());
    if (inMemory()) {
        memory().erase(key);
        return;
    }
    CFStringRef k = name(key);
    CFPreferencesSetAppValue(k, nullptr, kCFPreferencesCurrentApplication);
    CFPreferencesAppSynchronize(kCFPreferencesCurrentApplication);
    CFRelease(k);
}

void useMemory(bool enabled) {
    std::lock_guard<std::mutex> lock(guard());
    inMemory() = enabled;
    memory().clear();
}

}
