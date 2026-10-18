#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>

#include "driver/gpio.h"
#include "esp_sleep.h"

#include "_preference.h"
#include "_sntp.h"
#include "dashboard_cache.h"
#include "dashboard_client.h"
#include "led.h"
#include "refresh_scheduler.h"
#include "screen_ink.h"
#include "version.h"
#include "wiring.h"

namespace {

constexpr uint32_t WIFI_TIMEOUT_SECONDS = 10;
constexpr uint32_t SNTP_WAIT_MS = 6000;
constexpr uint32_t SCREEN_WAIT_MS = 120000;
constexpr uint32_t BUTTON_RELEASE_WAIT_MS = 2000;

WiFiManager wifiManager;
bool mainFlowComplete = false;

bool waitForSntp() {
    const uint32_t started = millis();
    while (_sntp_status() == SYNC_STATUS_IN_PROGRESS && millis() - started < SNTP_WAIT_MS) {
        delay(50);
    }
    if (_sntp_status() == SYNC_STATUS_IN_PROGRESS) _sntp_stop();
    return _sntp_status() == SYNC_STATUS_OK;
}

bool waitForScreen() {
    const uint32_t started = millis();
    while (si_screen_status() == 0 && millis() - started < SCREEN_WAIT_MS) delay(50);
    if (si_screen_status() == 0) {
        Serial.println("[Screen] refresh timeout; forcing shutdown path");
        si_screen_stop();
        return false;
    }
    return si_screen_status() == 1;
}

dashboard::PowerMode configuredPowerMode(uint16_t& externalMinutes) {
    Preferences preferences;
    preferences.begin(PREF_NAMESPACE, true);
    const uint8_t raw = preferences.getUChar(PREF_POWER_MODE, 0);
    externalMinutes = preferences.getUShort(PREF_EXT_MINUTES, 60);
    preferences.end();
    if (raw == 1) return dashboard::PowerMode::Saver;
    if (raw == 2) return dashboard::PowerMode::ExternalPower;
    return dashboard::PowerMode::Balanced;
}

void recordSuccessfulRefresh() {
    Preferences preferences;
    preferences.begin(PREF_NAMESPACE, false);
    preferences.putUInt(PREF_REFRESH_COUNT, preferences.getUInt(PREF_REFRESH_COUNT, 0) + 1);
    preferences.end();
}

bool buttonReleasedStable() {
    pinMode(KEY_M, INPUT_PULLUP);
    const uint32_t started = millis();
    uint32_t highSince = 0;
    while (millis() - started < BUTTON_RELEASE_WAIT_MS) {
        if (digitalRead(KEY_M) == HIGH) {
            if (highSince == 0) highSince = millis();
            if (millis() - highSince >= 50) return true;
        } else {
            highSince = 0;
        }
        delay(10);
    }
    return false;
}

void enterDeepSleep() {
    _sntp_stop();
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);

    uint16_t externalMinutes = 60;
    const dashboard::PowerMode mode = configuredPowerMode(externalMinutes);
    time_t now = 0;
    time(&now);
    const uint32_t seconds = dashboard::nextWakeSeconds(now, mode, externalMinutes);
    Serial.printf("[Sleep] next wake in %lu seconds\n", static_cast<unsigned long>(seconds));
    esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL);
    if (buttonReleasedStable()) {
        esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(KEY_M), LOW);
    } else {
        Serial.println("[Sleep] button still held; external wake disabled for this cycle");
    }

    gpio_deep_sleep_hold_dis();
    gpio_reset_pin(static_cast<gpio_num_t>(SPI_CS));
    gpio_reset_pin(static_cast<gpio_num_t>(SPI_DC));
    gpio_reset_pin(static_cast<gpio_num_t>(SPI_RST));
    gpio_reset_pin(static_cast<gpio_num_t>(SPI_BUSY));
    gpio_reset_pin(static_cast<gpio_num_t>(SPI_MOSI));
    gpio_reset_pin(static_cast<gpio_num_t>(SPI_MISO));
    gpio_reset_pin(static_cast<gpio_num_t>(SPI_SCK));
    Serial.flush();
    esp_deep_sleep_start();
}

void runMainFlow() {
    dashboard::Snapshot snapshot;
    const bool cacheAvailable = dashboard::dashboard_cache_load(snapshot);
    bool haveSnapshot = cacheAvailable;
    bool offline = cacheAvailable;
    if (cacheAvailable) Serial.println("[Dashboard] valid cached snapshot loaded");

    WiFi.mode(WIFI_STA);
    wifiManager.setHostname("J-Calendar");
    wifiManager.setEnableConfigPortal(false);
    wifiManager.setConnectTimeout(WIFI_TIMEOUT_SECONDS);
    const bool wifiConnected = wifiManager.autoConnect();
    Serial.printf("[WiFi] %s\n", wifiConnected ? "connected" : "unavailable");

    if (wifiConnected) {
        _sntp_exec();
        Serial.printf("[SNTP] %s\n", waitForSntp() ? "synchronized" : "timeout/failed");

        dashboard::Snapshot fresh;
        String json;
        const dashboard::ClientResult result = dashboard::dashboard_fetch(fresh, json);
        Serial.printf("[Dashboard] fetch=%s\n", dashboard::clientResultName(result));
        if (result == dashboard::ClientResult::Ok) {
            snapshot = fresh;
            haveSnapshot = true;
            offline = false;
            if (!dashboard::dashboard_cache_store(json.c_str(), json.length())) {
                Serial.println("[Dashboard] cache update failed; continuing with fresh snapshot");
            }
        }
    }

    if (haveSnapshot) {
        si_dashboard(snapshot, offline);
        if (waitForScreen()) recordSuccessfulRefresh();
    } else {
        Serial.println("[Dashboard] no usable fresh or cached snapshot; preserving current e-paper image");
    }
    enterDeepSleep();
}

} // namespace

void setup() {
    delay(10);
    Serial.begin(115200);
    setenv("TZ", "CST-8", 1);
    tzset();
    Serial.printf("\n=== J-Calendar %s ===\n", J_VERSION);
    Serial.printf("[Wake] cause=%d\n", static_cast<int>(esp_sleep_get_wakeup_cause()));
    led_init();
    led_on();
    delay(100);
    led_off();
}

void loop() {
    if (mainFlowComplete) {
        delay(100);
        return;
    }
    mainFlowComplete = true;
    runMainFlow();
}
