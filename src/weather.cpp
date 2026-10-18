#include "weather.h"
#include "qweather_jwt.h"

#include <_preference.h>

TaskHandle_t WEATHER_HANDLER;

String _qweather_host;
String _qweather_jwt_kid;
String _qweather_jwt_sub;
String _qweather_jwt_private;
String _qweather_loc;

int8_t _weather_status = -1;
int8_t _weather_type = -1;
Weather _weather_now = {};
DailyWeather dailyWeather[3] = {};
DailyForecast _daily_forecast = {
    .weather = dailyWeather,
    .length = 3
};

static String normalizeQWeatherHost(String host) {
    host.trim();
    host.toLowerCase();
    if (host.startsWith("https://")) host.remove(0, 8);
    if (host.startsWith("http://")) host.remove(0, 7);
    const int path = host.indexOf('/');
    if (path >= 0) host.remove(path);
    while (host.endsWith(".")) host.remove(host.length() - 1);
    return host;
}

static bool isValidQWeatherHost(const String& host) {
    if (host.isEmpty() || host.length() > 63 ||
        !host.endsWith(".qweatherapi.com")) return false;
    for (size_t i = 0; i < host.length(); ++i) {
        const char c = host.charAt(i);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '-')) {
            return false;
        }
    }
    return true;
}

int8_t weather_type() {
    return _weather_type;
}

int8_t weather_status() {
    return _weather_status;
}
Weather* weather_data_now() {
    return &_weather_now;
}
DailyForecast* weather_data_daily() {
    return &_daily_forecast;
}

void task_weather(void* param) {
    Serial.println("[Task] get weather begin...");

    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    _weather_type = pref.getString(PREF_QWEATHER_TYPE).compareTo("1") == 0 ? 1 : 0;
    pref.end();

    Serial.printf("Weather Type: %d\n", _weather_type);

    API<> api;

    String jwt;
    if (!qweatherBuildJwt(_qweather_jwt_kid, _qweather_jwt_sub,
                          _qweather_jwt_private, time(nullptr), jwt)) {
        Serial.println("QWeather JWT generation failed. Check time and JWT settings.");
        _weather_status = 2;
        WEATHER_HANDLER = NULL;
        vTaskDelete(NULL);
        return;
    }

    _weather_now = {};
    for (DailyWeather& item : dailyWeather) item = {};
    _daily_forecast.length = 3;
    _daily_forecast.updateTime = "";

    // 实时天气
    bool success;
    if (_weather_type == 0) {
        success = api.getForecastDaily(_daily_forecast, _qweather_host.c_str(), jwt.c_str(), _qweather_loc.c_str());
    } else {
        const bool nowOk = api.getWeatherNow(_weather_now, _qweather_host.c_str(), jwt.c_str(), _qweather_loc.c_str());
        const bool forecastOk = api.getForecastDaily(_daily_forecast, _qweather_host.c_str(), jwt.c_str(), _qweather_loc.c_str());
        // The current dashboard also renders the three-day forecast. Do not expose
        // partially populated weather data as a successful refresh.
        success = nowOk && forecastOk;
    }
    if (success) {
        _weather_status = 1;
    } else {
        _weather_status = 2;
    }

    Serial.println("[Task] get weather end...");
    WEATHER_HANDLER = NULL;
    vTaskDelete(NULL);
}

void weather_exec(int status) {
    _weather_status = status;
    if (status > 0) {
        return;
    }

    if (!WiFi.isConnected()) {
        _weather_status = 2;
        return;
    }

    // Preference 获取配置信息。
    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    _qweather_host = normalizeQWeatherHost(pref.getString(PREF_QWEATHER_HOST, ""));
    _qweather_jwt_kid = pref.getString(PREF_QW_JWT_KID, "");
    _qweather_jwt_sub = pref.getString(PREF_QW_JWT_SUB, "");
    _qweather_jwt_private = pref.getString(PREF_QW_JWT_PRIV, "");
    _qweather_loc = pref.getString(PREF_QWEATHER_LOC, "");
    pref.end();

    _qweather_jwt_kid.trim();
    _qweather_jwt_sub.trim();
    _qweather_jwt_private.trim();
    _qweather_loc.trim();
    if (!isValidQWeatherHost(_qweather_host) || _qweather_jwt_kid.isEmpty() ||
        _qweather_jwt_sub.isEmpty() || _qweather_jwt_private.isEmpty() || _qweather_loc.isEmpty()) {
        Serial.println("QWeather host/JWT/location invalid.");
        _weather_status = 3;
        return;
    }

    if (WEATHER_HANDLER != NULL) {
        vTaskDelete(WEATHER_HANDLER);
        WEATHER_HANDLER = NULL;
    }
    if (xTaskCreate(task_weather, "WeatherData", 1024 * 8, NULL, 2, &WEATHER_HANDLER) != pdPASS) {
        WEATHER_HANDLER = NULL;
        _weather_status = 2;
    }
}

void weather_stop() {
    if (WEATHER_HANDLER != NULL) {
        vTaskDelete(WEATHER_HANDLER);
        WEATHER_HANDLER = NULL;
    }
    _weather_status = 2;
}
