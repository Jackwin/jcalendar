#include "deepseek.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Preferences.h>
#include "_preference.h"

#if __has_include("openai_usage_secrets.h")
#include "openai_usage_secrets.h"
#endif

#define DS_BASE "https://api.deepseek.com"

TaskHandle_t DEEPSEEK_HANDLER = NULL;

int8_t _deepseek_status = -1;
DeepSeekData _deepseek_data = {};
String _ds_key;

int8_t deepseek_status() { return _deepseek_status; }
DeepSeekData* deepseek_data() { return &_deepseek_data; }

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
            JsonArray infos = doc["balance_infos"].as<JsonArray>();
            if (!infos.isNull() && infos.size() > 0) {
                _deepseek_data.isAvailable = doc["is_available"] | false;
                JsonObject info = infos[0];
                _deepseek_data.currency = info["currency"].as<const char*>();
                _deepseek_data.totalBalance = info["total_balance"].as<const char*>();
                _deepseek_data.grantedBalance = info["granted_balance"].as<const char*>();
                _deepseek_data.toppedUpBalance = info["topped_up_balance"].as<const char*>();
                ok = _deepseek_data.totalBalance.length() > 0;
            }
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
    Serial.println("[Task] deepseek balance begin...");
    _deepseek_data.usageStatus = DEEPSEEK_UNSUPPORTED;
    _deepseek_data.hasUsage = false;
    bool balOk = callDeepSeekBalance(_ds_key);
    _deepseek_data.balanceStatus = balOk ? DEEPSEEK_OK : DEEPSEEK_ERROR;
    _deepseek_status = balOk ? DEEPSEEK_OK : DEEPSEEK_ERROR;
    Serial.printf("[Task] deepseek balance end. ok=%d usage=unsupported balance=%s %s\n",
                  balOk,
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
        _deepseek_data.balanceStatus = DEEPSEEK_ERROR;
        _deepseek_status = DEEPSEEK_ERROR;
        return;
    }

    Preferences pref;
    pref.begin(PREF_NAMESPACE, true);
    _ds_key = pref.getString(PREF_DS_KEY, "");
    pref.end();

#if __has_include("openai_usage_secrets.h")
    if (String(DEEPSEEK_API_KEY).length() > 0) {
        _ds_key = DEEPSEEK_API_KEY;
    }
#endif

    if (_ds_key.length() == 0) {
        Serial.println("DeepSeek key not configured.");
        _deepseek_data.usageStatus = DEEPSEEK_UNSUPPORTED;
        _deepseek_data.balanceStatus = DEEPSEEK_NOT_CONFIGURED;
        _deepseek_status = DEEPSEEK_NOT_CONFIGURED;
        return;
    }

    if (DEEPSEEK_HANDLER != NULL) {
        vTaskDelete(DEEPSEEK_HANDLER);
        DEEPSEEK_HANDLER = NULL;
    }
    xTaskCreate(task_deepseek, "DS_Balance", 1024 * 8, NULL, 2, &DEEPSEEK_HANDLER);
}

void deepseek_exec_balance_only() {
    deepseek_exec(DEEPSEEK_LOADING);
}

void deepseek_stop() {
    if (DEEPSEEK_HANDLER != NULL) {
        vTaskDelete(DEEPSEEK_HANDLER);
        DEEPSEEK_HANDLER = NULL;
    }
    _deepseek_status = 2;
}
