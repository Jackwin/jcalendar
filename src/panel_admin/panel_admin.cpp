#include "panel_admin.h"

#include <ArduinoJson.h>
#include <Preferences.h>

#include "_preference.h"
#include "panel_page.h"

namespace panel {
namespace {

constexpr size_t MAX_STORED_JSON_BYTES = 3900;

bool validText(const String& text) {
    if (text.isEmpty() || text.length() > MAX_TEXT_BYTES) return false;
    for (size_t i = 0; i < text.length();) {
        const uint8_t first = static_cast<uint8_t>(text[i]);
        if (first < 0x20 || first == 0x7f) return false;
        if (first < 0x80) { ++i; continue; }
        const uint8_t count = first >= 0xc2 && first <= 0xdf ? 2
            : first >= 0xe0 && first <= 0xef ? 3
            : first >= 0xf0 && first <= 0xf4 ? 4 : 0;
        if (count == 0 || i + count > text.length()) return false;
        uint32_t codepoint = first & (0x7f >> count);
        for (uint8_t offset = 1; offset < count; ++offset) {
            const uint8_t byte = static_cast<uint8_t>(text[i + offset]);
            if ((byte & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        if ((count == 2 && codepoint < 0x80)
            || (count == 3 && codepoint < 0x800)
            || (count == 4 && codepoint < 0x10000)
            || (codepoint >= 0xd800 && codepoint <= 0xdfff)
            || codepoint > 0x10ffff) return false;
        i += count;
    }
    return true;
}

bool readText(JsonVariantConst value, String& result) {
    if (!value.is<const char*>()) return false;
    result = value.as<String>();
    result.trim();
    return validText(result);
}

} // namespace

const char* layoutName(Layout layout) {
    switch (layout) {
        case Layout::Courses: return "courses";
        case Layout::Tasks: return "tasks";
        default: return "dashboard";
    }
}

bool parseJson(const String& json, Config& config, String& error) {
    if (json.length() > MAX_STORED_JSON_BYTES) {
        error = "配置超过 NVS 容量";
        return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, json) || !doc.is<JsonObject>()) {
        error = "JSON 格式无效";
        return false;
    }
    JsonObjectConst root = doc.as<JsonObjectConst>();
    if (!root["version"].is<int>() || root["version"].as<int>() != 1
        || !root["layout"].is<const char*>()
        || !root["tasks"].is<JsonArrayConst>()
        || !root["courses"].is<JsonArrayConst>()) {
        error = "配置版本或字段无效";
        return false;
    }
    Config candidate;
    const String layout = root["layout"].as<String>();
    if (layout == "dashboard") candidate.layout = Layout::Dashboard;
    else if (layout == "courses") candidate.layout = Layout::Courses;
    else if (layout == "tasks") candidate.layout = Layout::Tasks;
    else {
        error = "显示模板无效";
        return false;
    }
    JsonArrayConst tasks = root["tasks"].as<JsonArrayConst>();
    JsonArrayConst courses = root["courses"].as<JsonArrayConst>();
    if (tasks.size() > MAX_TASKS || courses.size() > MAX_COURSES) {
        error = "待办或课程数量超限";
        return false;
    }
    for (JsonVariantConst item : tasks) {
        if (!item.is<JsonObjectConst>()) { error = "待办格式无效"; return false; }
        JsonObjectConst task = item.as<JsonObjectConst>();
        if (!readText(task["text"], candidate.tasks[candidate.taskCount].text)
            || !task["done"].is<bool>()) {
            error = "待办文字或完成状态无效";
            return false;
        }
        candidate.tasks[candidate.taskCount++].done = task["done"].as<bool>();
    }
    for (JsonVariantConst item : courses) {
        if (!item.is<JsonObjectConst>()) { error = "课程格式无效"; return false; }
        JsonObjectConst course = item.as<JsonObjectConst>();
        if (!course["day"].is<int>() || course["day"].as<int>() < 1
            || course["day"].as<int>() > 7
            || !readText(course["text"], candidate.courses[candidate.courseCount].text)) {
            error = "课程星期或文字无效";
            return false;
        }
        candidate.courses[candidate.courseCount++].day = course["day"].as<uint8_t>();
    }
    config = candidate;
    error = "";
    return true;
}

String toJson(const Config& config) {
    JsonDocument doc;
    doc["version"] = 1;
    doc["layout"] = layoutName(config.layout);
    JsonArray tasks = doc["tasks"].to<JsonArray>();
    for (uint8_t i = 0; i < config.taskCount; ++i) {
        JsonObject task = tasks.add<JsonObject>();
        task["text"] = config.tasks[i].text;
        task["done"] = config.tasks[i].done;
    }
    JsonArray courses = doc["courses"].to<JsonArray>();
    for (uint8_t i = 0; i < config.courseCount; ++i) {
        JsonObject course = courses.add<JsonObject>();
        course["day"] = config.courses[i].day;
        course["text"] = config.courses[i].text;
    }
    String json;
    serializeJson(doc, json);
    return json;
}

bool load(Config& config) {
    config = Config{};
    Preferences preferences;
    if (!preferences.begin(PREF_NAMESPACE, true)) return false;
    const String json = preferences.isKey(PREF_PANEL_CONFIG)
        ? preferences.getString(PREF_PANEL_CONFIG, "") : "";
    preferences.end();
    if (json.isEmpty()) return true;
    String error;
    if (parseJson(json, config, error)) return true;
    config = Config{};
    Serial.printf("[Panel] invalid saved config: %s\n", error.c_str());
    return false;
}

bool save(const String& json, String& error) {
    Config candidate;
    if (!parseJson(json, candidate, error)) return false;
    const String normalized = toJson(candidate);
    if (normalized.length() > MAX_STORED_JSON_BYTES) {
        error = "配置超过 NVS 容量";
        return false;
    }
    Preferences preferences;
    if (!preferences.begin(PREF_NAMESPACE, false)) {
        error = "无法打开设备存储";
        return false;
    }
    const bool written = preferences.putString(PREF_PANEL_CONFIG, normalized) == normalized.length();
    preferences.end();
    if (!written) error = "保存失败，请重试";
    else Serial.printf("[Panel] saved layout=%s tasks=%u courses=%u\n",
                       layoutName(candidate.layout), candidate.taskCount, candidate.courseCount);
    return written;
}

const char* html() { return PANEL_PAGE_HTML; }

} // namespace panel
