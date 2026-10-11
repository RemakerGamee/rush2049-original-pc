#pragma once

#include <chrono>
#include <exception>
#include <thread>

#if defined(__linux__)
#include <time.h>
#endif

namespace ultramodern {
#if defined(__linux__)
// Emulated hardware counters must also ignore host frequency slewing.
struct RuntimeRawClock {
    using rep = std::chrono::nanoseconds::rep;
    using period = std::chrono::nanoseconds::period;
    using duration = std::chrono::nanoseconds;
    using time_point = std::chrono::time_point<RuntimeRawClock>;
    static constexpr bool is_steady = true;

    static time_point now() noexcept {
        timespec value{};
        if (::clock_gettime(CLOCK_MONOTONIC_RAW, &value) != 0) {
            std::terminate();
        }
        return time_point{std::chrono::seconds{value.tv_sec} + std::chrono::nanoseconds{value.tv_nsec}};
    }
};
using runtime_clock = RuntimeRawClock;
#else
using runtime_clock = std::chrono::steady_clock;
#endif
static_assert(runtime_clock::is_steady);

inline void wait_for_runtime_deadline(runtime_clock::time_point deadline) {
    // Linux relative sleeps follow its slewed monotonic clock, even when
    // the deadline belongs to a raw clock. Recheck the actual deadline.
    for (;;) {
        const auto now = runtime_clock::now();
        if (now >= deadline) {
            return;
        }
        std::this_thread::sleep_for(deadline - now);
    }
}
}
