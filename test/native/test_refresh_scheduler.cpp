#include <assert.h>
#include <stdlib.h>

#include "refresh_scheduler.h"

static time_t localTime(int year, int month, int day, int hour, int minute, int second = 0) {
    struct tm value = {};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_sec = second;
    value.tm_isdst = -1;
    return mktime(&value);
}

int main() {
    setenv("TZ", "CST-8", 1);
    tzset();
    using dashboard::PowerMode;
    assert(dashboard::nextWakeSeconds(localTime(2026, 10, 18, 6, 59), PowerMode::Balanced) == 65);
    assert(dashboard::nextWakeSeconds(localTime(2026, 10, 18, 7, 1), PowerMode::Balanced) == 7145);
    assert(dashboard::nextWakeSeconds(localTime(2026, 10, 18, 23, 59), PowerMode::Balanced) == 365);
    assert(dashboard::nextWakeSeconds(localTime(2026, 10, 18, 12, 31), PowerMode::Saver) == 25145);
    assert(dashboard::nextWakeSeconds(localTime(2026, 10, 18, 10, 14), PowerMode::ExternalPower, 30) == 965);
    assert(dashboard::nextWakeSeconds(0, PowerMode::Balanced) == 7200);
    return 0;
}
