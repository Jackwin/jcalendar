#ifndef JCALENDAR_REFRESH_SCHEDULER_H
#define JCALENDAR_REFRESH_SCHEDULER_H

#include <stdint.h>
#include <time.h>

namespace dashboard {

enum class PowerMode : uint8_t {
    Balanced = 0,
    Saver = 1,
    ExternalPower = 2,
};

// Returns a bounded number of seconds until the next local scheduled refresh.
// The process timezone must already match the configured dashboard timezone.
uint32_t nextWakeSeconds(time_t now, PowerMode mode, uint16_t externalIntervalMinutes = 60);

} // namespace dashboard

#endif
