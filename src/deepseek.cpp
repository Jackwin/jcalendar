#include "deepseek.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include <time.h>

#include "_preference.h"

// 调用的 DeepSeek 模型
#define DS_MODEL_FLASH "deepseek-v4-flash"
#define DS_MODEL_PRO   "deepseek-v4-pro"
#define DS_BASE "https://api.deepseek.com"

TaskHandle_t DEEPSEEK_HANDLER = NULL;

int8_t _deepseek_status = -1;
DeepSeekData _deepseek_data = {};
String _ds_key;

int8_t deepseek_status() { return _deepseek_status; }
DeepSeekData* deepseek_data() { return &_deepseek_data; }

// 今日日期 YYYYMMDD
static String todayStr() {
    time_t now = 0;
    time(&now);
    struct tm t;
    localtime_r(&now, &t);
    char buf[9];
    snprintf(buf, sizeof(buf), "%04d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    return String(buf);
}

// 读取今日累计 token
static void loadTodayTokens() {
    Preferences pref;
    pref.begin(PREF_NAMESPACE, true);
    String storedDate = pref.getString(PREF_DS_DATE, "");
    if (storedDate == todayStr()) {
        _deepseek_data.tokensTodayFlash = pref.getUInt(PREF_DS_TODAY_FLASH, 0);
        _deepseek_data.tokensTodayPro   = pref.getUInt(PREF_DS_TODAY_PRO, 0);
    } else {
        _deepseek_data.tokensTodayFlash = 0;
        _deepseek_data.tokensTodayPro   = 0;
    }
    pref.end();
}

// 累计 token（跨日重置）
static void addTodayTokens(uint32_t flashAdd, uint32_t proAdd) {
    String today = todayStr();
    Preferences pref;
    pref.begin(PREF_NAMESPACE, false);
    String storedDate = pref.getString(PREF_DS_DATE, "");
    if (storedDate != today) {
        _deepseek_data.tokensTodayFlash = 0;
        _deepseek_data.tokensTodayPro   = 0;
    }
    _deepseek_data.tokensTodayFlash += flashAdd;
    _deepseek_data.tokensTodayPro   += proAdd;
    pref.putString(PREF_DS_DATE, today);
    pref.putUInt(PREF_DS_TODAY_FLASH, _deepseek_data.tokensTodayFlash);
    pref.putUInt(PREF_DS_TODAY_PRO,   _deepseek_data.tokensTodayPro);
    pref.end();
}

// 调用 deepseek 模型，解析 usage
static bool callDeepSeekModel(const String& key, const String& model, bool isFlash) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, String(DS_BASE) + "/chat/completions")) {
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.addHeader("Authorization", "Bearer " + key);
    http.setTimeout(15000);

    String body = String("{\"model\":\"") + model +
                  "\",\"messages\":[{\"role\":\"user\",\"content\":"
                  "\"用一句话(不超过15字)给出今日寄语\"}],\"max_tokens\":32}";

    int code = http.POST(body);
    bool ok = false;
    if (code == HTTP_CODE_OK) {
        String resp = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, resp);
        if (!err) {
            _deepseek_data.content = doc["choices"][0]["message"]["content"].as<const char*>();
            uint32_t total = doc["usage"]["total_tokens"] | 0;
            if (isFlash) {
                _deepseek_data.flashPromptTokens     = doc["usage"]["prompt_tokens"] | 0;
                _deepseek_data.flashCompletionTokens = doc["usage"]["completion_tokens"] | 0;
                _deepseek_data.flashTotalTokens      = total;
            } else {
                _deepseek_data.proPromptTokens       = doc["usage"]["prompt_tokens"] | 0;
                _deepseek_data.proCompletionTokens   = doc["usage"]["completion_tokens"] | 0;
                _deepseek_data.proTotalTokens        = total;
            }
            ok = total > 0;
        } else {
            Serial.print(F("[DeepSeek] chat parse failed: "));
            Serial.println(err.c_str());
        }
    } else {
        Serial.printf("[DeepSeek] %s HTTP %d\n", model.c_str(), code);
    }
    http.end();
    return ok;
}

// 查询账户余额
static bool callDeepSeekBalance(const String& key) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    if (!http.begin(client, String(DS_BASE) + "/user/balance")) {
        return false;
    }
    http.addHeader("Authorization", "Bearer " + key);
    http.setTimeout(10000);

    int code = http.GET();
    bool ok = false;
    if (code == HTTP_CODE_OK) {
        String resp = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, resp);
        if (!err) {
            _deepseek_data.isAvailable = doc["is_available"] | false;
            JsonObject info = doc["balance_infos"][0];
            _deepseek_data.currency = info["currency"].as<const char*>();
            _deepseek_data.totalBalance = info["total_balance"].as<const char*>();
            _deepseek_data.grantedBalance = info["granted_balance"].as<const char*>();
            _deepseek_data.toppedUpBalance = info["topped_up_balance"].as<const char*>();
            ok = _deepseek_data.totalBalance.length() > 0;
        } else {
            Serial.print(F("[DeepSeek] balance parse failed: "));
            Serial.println(err.c_str());
        }
    } else {
        Serial.printf("[DeepSeek] balance HTTP %d\n", code);
    }
    http.end();
    return ok;
}

void task_deepseek(void* param) {
    Serial.println("[Task] deepseek begin...");

    loadTodayTokens();

    // 分别调用 flash 和 pro 模型
    bool flashOk = callDeepSeekModel(_ds_key, DS_MODEL_FLASH, true);
    if (flashOk) {
        addTodayTokens(_deepseek_data.flashTotalTokens, 0);
    }
    bool proOk = callDeepSeekModel(_ds_key, DS_MODEL_PRO, false);
    if (proOk) {
        addTodayTokens(0, _deepseek_data.proTotalTokens);
    }
    bool balOk = callDeepSeekBalance(_ds_key);

    _deepseek_status = (flashOk || proOk || balOk) ? 1 : 2;
    Serial.printf("[Task] deepseek end. flash=%d pro=%d bal=%d today(flash=%u,pro=%u) balance=%s %s\n",
                  flashOk, proOk, balOk,
                  _deepseek_data.tokensTodayFlash, _deepseek_data.tokensTodayPro,
                  _deepseek_data.currency.c_str(), _deepseek_data.totalBalance.c_str());

    DEEPSEEK_HANDLER = NULL;
    vTaskDelete(NULL);
}

void deepseek_exec(int status) {
    _deepseek_status = status;
    if (status > 0) {
        return;
    }

    if (!WiFi.isConnected()) {
        _deepseek_status = 2;
        return;
    }

    Preferences pref;
    pref.begin(PREF_NAMESPACE, true);
    _ds_key = pref.getString(PREF_DS_KEY, "");
    pref.end();

    if (_ds_key.length() == 0) {
        Serial.println("DeepSeek key not configured.");
        _deepseek_status = 3;
        return;
    }

    if (DEEPSEEK_HANDLER != NULL) {
        vTaskDelete(DEEPSEEK_HANDLER);
        DEEPSEEK_HANDLER = NULL;
    }
    xTaskCreate(task_deepseek, "DeepSeek", 1024 * 10, NULL, 2, &DEEPSEEK_HANDLER);
}

// 仅查余额的任务（每小时调用，不消耗 token）
void task_deepseek_balance(void* param) {
    Serial.println("[Task] deepseek balance only...");

    loadTodayTokens();

    bool balOk = callDeepSeekBalance(_ds_key);
    _deepseek_status = balOk ? 1 : 2;
    Serial.printf("[Task] deepseek balance end. ok=%d today(flash=%u,pro=%u) balance=%s %s\n",
                  balOk, _deepseek_data.tokensTodayFlash, _deepseek_data.tokensTodayPro,
                  _deepseek_data.currency.c_str(), _deepseek_data.totalBalance.c_str());

    DEEPSEEK_HANDLER = NULL;
    vTaskDelete(NULL);
}

void deepseek_exec_balance_only() {
    _deepseek_status = 0; // 标记进行中，防止重复调用

    if (!WiFi.isConnected()) {
        _deepseek_status = 2;
        return;
    }

    Preferences pref;
    pref.begin(PREF_NAMESPACE, true);
    _ds_key = pref.getString(PREF_DS_KEY, "");
    pref.end();

    if (_ds_key.length() == 0) {
        Serial.println("DeepSeek key not configured.");
        _deepseek_status = 3;
        return;
    }

    if (DEEPSEEK_HANDLER != NULL) {
        vTaskDelete(DEEPSEEK_HANDLER);
        DEEPSEEK_HANDLER = NULL;
    }
    xTaskCreate(task_deepseek_balance, "DS_Bal", 1024 * 8, NULL, 2, &DEEPSEEK_HANDLER);
}

void deepseek_stop() {
    if (DEEPSEEK_HANDLER != NULL) {
        vTaskDelete(DEEPSEEK_HANDLER);
        DEEPSEEK_HANDLER = NULL;
    }
    _deepseek_status = 2;
}
