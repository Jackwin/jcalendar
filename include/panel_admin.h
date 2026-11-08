#ifndef JCALENDAR_PANEL_ADMIN_H
#define JCALENDAR_PANEL_ADMIN_H

#include <Arduino.h>

namespace panel {

constexpr uint8_t MAX_TASKS = 12;
constexpr uint8_t MAX_COURSES = 35;
constexpr size_t MAX_TEXT_BYTES = 60;

enum class Layout : uint8_t { Dashboard, Courses, Tasks };

struct Task {
    String text;
    bool done = false;
};

struct Course {
    uint8_t day = 1; // ISO weekday: Monday=1, Sunday=7.
    String text;
};

struct Config {
    Layout layout = Layout::Dashboard;
    Task tasks[MAX_TASKS];
    Course courses[MAX_COURSES];
    uint8_t taskCount = 0;
    uint8_t courseCount = 0;
};

const char* layoutName(Layout layout);
bool parseJson(const String& json, Config& config, String& error);
String toJson(const Config& config);
bool load(Config& config);
bool save(const String& json, String& error);
const char* html();

} // namespace panel

#endif
