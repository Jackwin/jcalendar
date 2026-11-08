#ifndef JCALENDAR_DASHBOARD_RENDERER_H
#define JCALENDAR_DASHBOARD_RENDERER_H

#include <Arduino.h>

#include "dashboard_model.h"

namespace dashboard {

struct Rect {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
};

struct HomeLayout {
    Rect date;
    Rect weather;
    Rect openai;
    Rect deepseek;
    Rect focus;
    Rect status;
};

HomeLayout homeLayout();
bool layoutFits400x300(const HomeLayout& layout);
const UsageData* findUsage(const Snapshot& snapshot, const char* provider);
String formatTokenCount(uint64_t value);
String usageValue(const UsageData* usage);
String usageScopeLabel(const UsageData* usage);
String snapshotStateLabel(const Snapshot& snapshot, bool offline);

} // namespace dashboard

#endif
