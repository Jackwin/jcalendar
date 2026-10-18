#ifndef JCALENDAR_DASHBOARD_CACHE_H
#define JCALENDAR_DASHBOARD_CACHE_H

#include <Arduino.h>
#include <stddef.h>

#include "dashboard_model.h"

namespace dashboard {

bool dashboard_cache_begin();
bool dashboard_cache_load(Snapshot& snapshot);
bool dashboard_cache_store(const char* json, size_t length);

} // namespace dashboard

#endif
