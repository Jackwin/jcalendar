#include "refresh_scheduler.h"

#include <algorithm>

namespace dashboard {
namespace {

constexpr time_t MIN_VALID_TIME = 1577836800; // 2020-01-01
constexpr uint32_t FALLBACK_SECONDS = 2 * 60 * 60;

time_t localCandidate(const struct tm& base, int dayOffset, int minuteOfDay) {
    struct tm candidate = base;
    candidate.tm_mday += dayOffset;
    candidate.tm_hour = minuteOfDay / 60;
    candidate.tm_min = minuteOfDay % 60;
    candidate.tm_sec = 0;
    candidate.tm_isdst = -1;
    return mktime(&candidate);
}

uint32_t secondsUntilSchedule(time_t now, const int* minutes, size_t count) {
    struct tm local = {};
    localtime_r(&now, &local);
    time_t best = 0;
    for (int dayOffset = 0; dayOffset <= 1; ++dayOffset) {
        for (size_t i = 0; i < count; ++i) {
            const time_t candidate = localCandidate(local, dayOffset, minutes[i]);
            if (candidate > now && (best == 0 || candidate < best)) best = candidate;
        }
    }
    if (best <= now) return FALLBACK_SECONDS;
    const uint64_t seconds = static_cast<uint64_t>(best - now) + 5;
    return static_cast<uint32_t>(std::min<uint64_t>(seconds, 24ULL * 60ULL * 60ULL));
}

} // namespace

uint32_t nextWakeSeconds(time_t now, PowerMode mode, uint16_t externalIntervalMinutes) {
    if (now < MIN_VALID_TIME) return FALLBACK_SECONDS;
    if (mode == PowerMode::ExternalPower) {
        const uint32_t interval = (externalIntervalMinutes == 30 ? 30U : 60U) * 60U;
        return interval - static_cast<uint32_t>(now % interval) + 5;
    }
    if (mode == PowerMode::Saver) {
        static const int saver[] = {5, 7 * 60 + 30, 12 * 60 + 30, 19 * 60 + 30};
        return secondsUntilSchedule(now, saver, sizeof(saver) / sizeof(saver[0]));
    }
    static const int balanced[] = {
        5, 7 * 60, 9 * 60, 11 * 60, 13 * 60,
        15 * 60, 17 * 60, 19 * 60, 21 * 60, 23 * 60,
    };
    return secondsUntilSchedule(now, balanced, sizeof(balanced) / sizeof(balanced[0]));
}

} // namespace dashboard
