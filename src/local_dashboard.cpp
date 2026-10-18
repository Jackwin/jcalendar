#include "local_dashboard.h"

#include <ArduinoJson.h>
#include <Preferences.h>
#include "_preference.h"
#include "weather.h"
#include "deepseek.h"

namespace dashboard {

ClientResult local_dashboard_fetch(Snapshot& snapshot, String& validatedJson) {
    // The legacy clients run concurrently, with one bounded wait for both.
    weather_exec();
    deepseek_exec_balance_only();
    const uint32_t started = millis();
    while ((weather_status() == 0 || deepseek_status() == DEEPSEEK_LOADING)
           && millis() - started < 45000) delay(50);
    if (weather_status() == 0) weather_stop();
    if (deepseek_status() == DEEPSEEK_LOADING) deepseek_stop();
    Serial.printf("[Direct] weather=%d DeepSeek=%d\n", weather_status(), deepseek_status());

    const time_t now = time(nullptr);
    struct tm local = {}, utc = {};
    localtime_r(&now, &local);
    gmtime_r(&now, &utc);
    char date[11], timestamp[21];
    strftime(date, sizeof(date), "%Y-%m-%d", &local);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);

    JsonDocument doc;
    doc["schema_version"] = SCHEMA_VERSION;
    doc["generated_at"] = timestamp;
    doc["timezone"] = "Asia/Shanghai";
    doc["local_date"] = now >= 1609459200 ? date : "";
    doc["refresh_after_seconds"] = 7200;
    Preferences preferences;
    preferences.begin(PREF_NAMESPACE, true);
    String location = preferences.getString(PREF_QWEATHER_LOC, "");
    const String focus = preferences.getString(PREF_FOCUS_TEXT, "");
    preferences.end();
    // Keep the strict snapshot text limits for user-provided location IDs.
    if (location.length() > MAX_LOCATION_BYTES) location = "已配置位置";

    JsonObject weather = doc["weather"].to<JsonObject>();
    const bool weatherOk = weather_status() == 1;
    weather["status"] = weatherOk ? "ok" : weather_status() == 3 ? "not_configured" : "error";
    weather["source"] = "qweather";
    weather["observed_at"] = nullptr;
    weather["fetched_at"] = weatherOk ? timestamp : nullptr;
    weather["location_name"] = location.isEmpty() ? "和风天气" : location;
    weather["temp_c"] = nullptr;
    weather["temp_min_c"] = nullptr;
    weather["temp_max_c"] = nullptr;
    weather["condition"] = weather_status() == 3 ? "未配置" : "获取失败";
    weather["condition_code"] = "";
    if (weatherOk) {
        const DailyForecast* forecast = weather_data_daily();
        const DailyWeather& today = forecast->weather[0];
        weather["temp_min_c"] = today.tempMin;
        weather["temp_max_c"] = today.tempMax;
        weather["condition"] = today.textDay;
        weather["condition_code"] = String(today.iconDay);
        weather["observed_at"] = forecast->updateTime;
        if (weather_type() == 1) {
            const Weather* current = weather_data_now();
            weather["temp_c"] = current->temp;
            weather["condition"] = current->text;
            weather["condition_code"] = String(current->icon);
            weather["observed_at"] = current->updateTime;
        }
    }

    JsonObject usage = doc["usage"].to<JsonArray>().add<JsonObject>();
    usage["provider"] = "deepseek";
    usage["status"] = "unsupported";
    usage["source"] = "deepseek";
    usage["coverage"] = "unknown";
    usage["completeness"] = "unknown";
    usage["window_start"] = "";
    usage["window_end"] = "";
    usage["fetched_at"] = nullptr;
    usage["observed_at"] = nullptr;
    usage["input_tokens"] = nullptr;
    usage["output_tokens"] = nullptr;
    usage["total_tokens"] = nullptr;
    const DeepSeekData* ds = deepseek_data();
    const bool balanceOk = ds->balanceStatus == DEEPSEEK_OK;
    JsonObject balance = usage["balance"].to<JsonObject>();
    balance["status"] = balanceOk ? "ok"
        : ds->balanceStatus == DEEPSEEK_NOT_CONFIGURED ? "not_configured" : "error";
    balance["currency"] = ds->currency;
    balance["amount"] = balanceOk ? ds->totalBalance : String();
    balance["fetched_at"] = balanceOk ? timestamp : nullptr;
    doc["focus"]["type"] = "text";
    doc["focus"]["text"] = focus;

    String json;
    serializeJson(doc, json);
    const ClientResult result = dashboard_parse_json(json.c_str(), json.length(), snapshot);
    // Keep the previous cache if a configured provider failed this cycle.
    validatedJson = result == ClientResult::Ok && weather_status() != 2
        && ds->balanceStatus != DEEPSEEK_ERROR && (weatherOk || balanceOk) ? json : String();
    return result;
}
}
