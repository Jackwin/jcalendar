#include "dashboard_client.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <memory>
#include <Preferences.h>
#include "_preference.h"

#if __has_include("openai_usage_secrets.h")
#include "openai_usage_secrets.h"
#endif

#ifndef DASHBOARD_URL
#define DASHBOARD_URL ""
#endif
#ifndef DASHBOARD_TOKEN
#define DASHBOARD_TOKEN ""
#endif
#ifndef DASHBOARD_CA
#define DASHBOARD_CA ""
#endif

namespace dashboard {
namespace {

constexpr uint32_t HTTP_TIMEOUT_MS = 15000;

bool copyString(JsonVariantConst value, String& output, size_t maxBytes, bool nullable = false) {
    if (value.isNull()) {
        if (!nullable) return false;
        output = "";
        return true;
    }
    if (!value.is<const char*>()) return false;
    const char* text = value.as<const char*>();
    if (text == nullptr || strlen(text) > maxBytes) return false;
    output = text;
    return true;
}

bool parseOptionalInt(JsonVariantConst value, OptionalInt32& output) {
    if (value.isNull()) {
        output = {};
        return true;
    }
    if (!value.is<int32_t>()) return false;
    output.hasValue = true;
    output.value = value.as<int32_t>();
    return true;
}

bool parseOptionalCount(JsonVariantConst value, OptionalUInt64& output) {
    if (value.isNull()) {
        output = {};
        return true;
    }
    if (!value.is<uint64_t>()) return false;
    output.hasValue = true;
    output.value = value.as<uint64_t>();
    return true;
}

bool parseStatusField(JsonVariantConst value, Status& status) {
    return value.is<const char*>() && parseStatus(value.as<const char*>(), status);
}

bool parseCompletenessField(JsonVariantConst value, Completeness& completeness) {
    return value.is<const char*>() && parseCompleteness(value.as<const char*>(), completeness);
}

bool parseWeather(JsonObjectConst object, WeatherData& weather) {
    return parseStatusField(object["status"], weather.status)
        && copyString(object["source"], weather.source, 24)
        && copyString(object["observed_at"], weather.observedAt, 32, true)
        && copyString(object["fetched_at"], weather.fetchedAt, 32, true)
        && copyString(object["location_name"], weather.locationName, MAX_LOCATION_BYTES)
        && parseOptionalInt(object["temp_c"], weather.tempC)
        && parseOptionalInt(object["temp_min_c"], weather.tempMinC)
        && parseOptionalInt(object["temp_max_c"], weather.tempMaxC)
        && copyString(object["condition"], weather.condition, MAX_CONDITION_BYTES)
        && copyString(object["condition_code"], weather.conditionCode, 16);
}

bool parseBalance(JsonVariantConst value, BalanceData& balance) {
    if (value.isNull()) {
        balance = {};
        return true;
    }
    if (!value.is<JsonObjectConst>()) return false;
    JsonObjectConst object = value.as<JsonObjectConst>();
    return parseStatusField(object["status"], balance.status)
        && copyString(object["currency"], balance.currency, 8)
        && copyString(object["amount"], balance.amount, 32)
        && copyString(object["fetched_at"], balance.fetchedAt, 32, true);
}

bool parseUsage(JsonObjectConst object, UsageData& usage) {
    if (!copyString(object["provider"], usage.provider, 16)
        || !parseStatusField(object["status"], usage.status)
        || !copyString(object["source"], usage.source, 24)
        || !copyString(object["coverage"], usage.coverage, 24)
        || !parseCompletenessField(object["completeness"], usage.completeness)
        || !copyString(object["window_start"], usage.windowStart, 32)
        || !copyString(object["window_end"], usage.windowEnd, 32)
        || !copyString(object["fetched_at"], usage.fetchedAt, 32, true)
        || !copyString(object["observed_at"], usage.observedAt, 32, true)
        || !parseOptionalCount(object["input_tokens"], usage.inputTokens)
        || !parseOptionalCount(object["output_tokens"], usage.outputTokens)
        || !parseOptionalCount(object["total_tokens"], usage.totalTokens)
        || !parseBalance(object["balance"], usage.balance)) {
        return false;
    }

    const bool anyCount = usage.inputTokens.hasValue || usage.outputTokens.hasValue || usage.totalTokens.hasValue;
    const bool allCounts = usage.inputTokens.hasValue && usage.outputTokens.hasValue && usage.totalTokens.hasValue;
    if (anyCount && !allCounts && usage.completeness != Completeness::Partial) return false;
    if (allCounts && !hasValidTokenTotal(usage)) return false;
    return true;
}

ClientResult readBoundedBody(HTTPClient& http, String& body) {
    const int announced = http.getSize();
    if (announced > static_cast<int>(MAX_RESPONSE_BYTES)) return ClientResult::ResponseTooLarge;

    std::unique_ptr<char[]> buffer(new (std::nothrow) char[MAX_RESPONSE_BYTES + 1]);
    if (!buffer) return ClientResult::InvalidData;
    WiFiClient* stream = http.getStreamPtr();
    size_t used = 0;
    uint32_t lastProgress = millis();
    while ((http.connected() || stream->available() > 0)
           && (announced < 0 || used < static_cast<size_t>(announced))) {
        const size_t available = stream->available();
        if (available == 0) {
            if (millis() - lastProgress >= HTTP_TIMEOUT_MS) return ClientResult::Timeout;
            delay(10);
            continue;
        }
        const size_t remaining = MAX_RESPONSE_BYTES + 1 - used;
        const size_t wanted = min(available, remaining);
        used += stream->readBytes(reinterpret_cast<uint8_t*>(buffer.get()) + used, wanted);
        lastProgress = millis();
        if (used > MAX_RESPONSE_BYTES) return ClientResult::ResponseTooLarge;
    }
    if (announced >= 0 && used != static_cast<size_t>(announced)) return ClientResult::Timeout;
    buffer[used] = '\0';
    body = String(buffer.get(), used);
    return ClientResult::Ok;
}

} // namespace

ClientResult dashboard_parse_json(const char* data, size_t length, Snapshot& snapshot) {
    if (data == nullptr || length == 0) return ClientResult::InvalidJson;
    if (length > MAX_RESPONSE_BYTES) return ClientResult::ResponseTooLarge;

    JsonDocument document;
    const DeserializationError error = deserializeJson(document, data, length);
    if (error) return ClientResult::InvalidJson;
    JsonObjectConst root = document.as<JsonObjectConst>();
    if (root.isNull() || !root["schema_version"].is<uint16_t>()) return ClientResult::InvalidData;

    Snapshot parsed;
    parsed.schemaVersion = root["schema_version"].as<uint16_t>();
    if (!isSupportedSchema(parsed.schemaVersion)) return ClientResult::UnsupportedSchema;
    if (!copyString(root["generated_at"], parsed.generatedAt, 32)
        || !copyString(root["timezone"], parsed.timezone, 48)
        || !copyString(root["local_date"], parsed.localDate, 10)
        || !root["weather"].is<JsonObjectConst>()
        || !parseWeather(root["weather"].as<JsonObjectConst>(), parsed.weather)
        || !root["usage"].is<JsonArrayConst>()) {
        return ClientResult::InvalidData;
    }

    JsonArrayConst usageArray = root["usage"].as<JsonArrayConst>();
    if (usageArray.size() > MAX_USAGE_PROVIDERS) return ClientResult::InvalidData;
    for (JsonObjectConst usageObject : usageArray) {
        if (!parseUsage(usageObject, parsed.usage[parsed.usageCount])) return ClientResult::InvalidData;
        parsed.usageCount += 1;
    }

    if (!root["focus"].is<JsonObjectConst>()) return ClientResult::InvalidData;
    JsonObjectConst focus = root["focus"].as<JsonObjectConst>();
    if (!copyString(focus["type"], parsed.focus.type, 16)
        || !copyString(focus["text"], parsed.focus.text, MAX_FOCUS_BYTES)
        || !root["refresh_after_seconds"].is<uint32_t>()) {
        return ClientResult::InvalidData;
    }
    parsed.refreshAfterSeconds = root["refresh_after_seconds"].as<uint32_t>();
    snapshot = parsed;
    return ClientResult::Ok;
}

ClientResult dashboard_fetch(Snapshot& snapshot, String& validatedJson) {
    validatedJson = "";
    Preferences preferences;
    preferences.begin(PREF_NAMESPACE, true);
    const String url = preferences.getString(PREF_DASH_URL, DASHBOARD_URL);
    const String token = preferences.getString(PREF_DASH_TOKEN, DASHBOARD_TOKEN);
    const String ca = preferences.getString(PREF_DASH_CA, DASHBOARD_CA);
    preferences.end();
    if (!url.startsWith("https://") || token.isEmpty() || ca.isEmpty()) return ClientResult::NotConfigured;
    if (!WiFi.isConnected()) return ClientResult::WifiUnavailable;

    WiFiClientSecure client;
    client.setCACert(ca.c_str());
    client.setTimeout(HTTP_TIMEOUT_MS / 1000);
    HTTPClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    if (!http.begin(client, url)) return ClientResult::TlsConfigurationError;
    http.addHeader("Authorization", "Bearer " + token);
    http.addHeader("Accept", "application/json");

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        if (code == HTTP_CODE_UNAUTHORIZED) return ClientResult::Unauthorized;
        if (code == HTTP_CODE_TOO_MANY_REQUESTS) return ClientResult::RateLimited;
        return ClientResult::HttpError;
    }

    String body;
    const ClientResult readResult = readBoundedBody(http, body);
    http.end();
    if (readResult != ClientResult::Ok) return readResult;
    const ClientResult parseResult = dashboard_parse_json(body.c_str(), body.length(), snapshot);
    if (parseResult == ClientResult::Ok) validatedJson = body;
    return parseResult;
}

const char* clientResultName(ClientResult result) {
    switch (result) {
    case ClientResult::Ok: return "ok";
    case ClientResult::NotConfigured: return "not_configured";
    case ClientResult::WifiUnavailable: return "wifi_unavailable";
    case ClientResult::TlsConfigurationError: return "tls_configuration_error";
    case ClientResult::Unauthorized: return "unauthorized";
    case ClientResult::RateLimited: return "rate_limited";
    case ClientResult::HttpError: return "http_error";
    case ClientResult::ResponseTooLarge: return "response_too_large";
    case ClientResult::Timeout: return "timeout";
    case ClientResult::InvalidJson: return "invalid_json";
    case ClientResult::UnsupportedSchema: return "unsupported_schema";
    case ClientResult::InvalidData: return "invalid_data";
    }
    return "invalid_data";
}

} // namespace dashboard
