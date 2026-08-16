#include "openai_usage.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#if __has_include("openai_usage_secrets.h")
#include "openai_usage_secrets.h"
#define OPENAI_USAGE_CONFIGURED 1
#else
#define OPENAI_USAGE_CONFIGURED 0
#define OPENAI_USAGE_PROXY_URL ""
#define OPENAI_USAGE_PROXY_TOKEN ""
#define OPENAI_USAGE_PROXY_CA ""
#endif

namespace {
TaskHandle_t usageTask = nullptr;
int8_t usageStatus = -1;
OpenAIUsageData usageData = {};
bool requestUsage() {
    const String proxyUrl = OPENAI_USAGE_PROXY_URL;
    const String proxyToken = OPENAI_USAGE_PROXY_TOKEN;
    const String proxyCa = OPENAI_USAGE_PROXY_CA;
    if (!proxyUrl.startsWith("https://") || proxyToken.isEmpty() || proxyCa.isEmpty()) {
        Serial.println("[OpenAI Usage] HTTPS proxy URL, token, or CA is missing.");
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(proxyCa.c_str());
    HTTPClient http;
    if (!http.begin(client, proxyUrl)) return false;
    http.addHeader("Authorization", "Bearer " + proxyToken);
    http.addHeader("Accept", "application/json");
    http.setTimeout(15000);

    const int code = http.GET();
    bool ok = false;
    if (code == HTTP_CODE_OK) {
        JsonDocument doc;
        const DeserializationError err = deserializeJson(doc, http.getString());
        if (!err) {
            usageData.inputTokens = doc["input_tokens"] | 0;
            usageData.outputTokens = doc["output_tokens"] | 0;
            usageData.totalTokens = doc["total_tokens"] | (usageData.inputTokens + usageData.outputTokens);
            usageData.dateUtc = doc["date_utc"].as<const char*>();
            ok = true;
        } else {
            Serial.printf("[OpenAI Usage] JSON parse failed: %s\n", err.c_str());
        }
    } else {
        Serial.printf("[OpenAI Usage] proxy HTTP %d\n", code);
    }
    http.end();
    return ok;
}

void taskOpenAIUsage(void*) {
    const bool ok = requestUsage();
    usageStatus = ok ? 1 : 2;
    if (ok) {
        Serial.printf("[OpenAI Usage] UTC %s input=%u output=%u total=%u\n",
                      usageData.dateUtc.c_str(), usageData.inputTokens,
                      usageData.outputTokens, usageData.totalTokens);
    }
    usageTask = nullptr;
    vTaskDelete(nullptr);
}
} // namespace

int8_t openai_usage_status() { return usageStatus; }
OpenAIUsageData* openai_usage_data() { return &usageData; }

void openai_usage_exec() {
    usageStatus = 0;
    if (!WiFi.isConnected()) {
        usageStatus = 2;
        return;
    }

#if !OPENAI_USAGE_CONFIGURED
    Serial.println("[OpenAI Usage] Missing private include/openai_usage_secrets.h");
    usageStatus = 3;
    return;
#else
    if (String(OPENAI_USAGE_PROXY_URL).isEmpty()
        || String(OPENAI_USAGE_PROXY_TOKEN).isEmpty()
        || String(OPENAI_USAGE_PROXY_CA).isEmpty()) {
        usageStatus = 3;
        return;
    }
#endif
    if (usageTask != nullptr) {
        vTaskDelete(usageTask);
        usageTask = nullptr;
    }
    xTaskCreate(taskOpenAIUsage, "OpenAIUsage", 8192, nullptr, 2, &usageTask);
}

void openai_usage_stop() {
    if (usageTask != nullptr) {
        vTaskDelete(usageTask);
        usageTask = nullptr;
    }
    usageStatus = 2;
}
