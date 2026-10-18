#pragma once

#include <string>
#include <time.h>

namespace support::timing {

enum class Validity { valid, instrumented, unavailable, powerLimited, throttled, loaded };

inline Validity classify(bool resource, bool sanitized, bool lowPower, int pressure, double workload, int coreCount) {
    if (sanitized) return Validity::instrumented;
    if (resource) return Validity::valid;
    if (pressure < 0 || workload < 0 || coreCount < 1) return Validity::unavailable;
    if (lowPower) return Validity::powerLimited;
    if (pressure >= 2) return Validity::throttled;
    if (workload >= coreCount) return Validity::loaded;
    return Validity::valid;
}

Validity validity(bool resource = false);
std::string describe(Validity reason);
bool required();
bool forced();
void executed(bool passed, const char* expression, const char* file, int line);
void skipped(Validity reason, const char* expression, const char* file, int line);
double load();
int thermal();
int cores();
bool lowPower();

inline double threadSeconds() {
    timespec value{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value);
    return static_cast<double>(value.tv_sec) + static_cast<double>(value.tv_nsec) / 1e9;
}

}

#define LUDISSEY_TIMING_CHECK(resource, expression) \
    do { \
        auto timing_validity = support::timing::validity(resource); \
        if (timing_validity == support::timing::Validity::valid || (support::timing::forced() && timing_validity != support::timing::Validity::instrumented)) { \
            support::timing::executed(static_cast<bool>(expression), #expression, __FILE__, __LINE__); \
            CHECK(expression); \
        } else { \
            support::timing::skipped(timing_validity, #expression, __FILE__, __LINE__); \
            if (support::timing::required()) FAIL_CHECK("misura richiesta ma non valida (" << support::timing::describe(timing_validity) << "): " #expression); \
            else WARN("MISURA NON VALIDA (" << support::timing::describe(timing_validity) << "): " #expression); \
        } \
    } while (false)

#define TIMED_CHECK(expression) LUDISSEY_TIMING_CHECK(false, expression)
#define UNINSTRUMENTED_CHECK(expression) LUDISSEY_TIMING_CHECK(true, expression)
