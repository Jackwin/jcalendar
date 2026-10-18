#include <Arduino.h>
#include <DNSServer.h>
#include <OneButton.h>
#include <Preferences.h>
#include <WebServer.h>
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
constexpr uint32_t GESTURE_WINDOW_MS = 4500;
constexpr uint32_t CONFIG_WINDOW_MS = 180000;
constexpr uint16_t CONFIG_VERSION = 1;
constexpr uint8_t PAGE_COUNT = 2;
constexpr uint8_t PAGE_DASHBOARD = 0;
constexpr uint8_t PAGE_CALENDAR = 1;

WiFiManager wifiManager;
OneButton modeButton(KEY_M, true, true);
bool mainFlowComplete = false;
bool configSaved = false;
bool factoryResetRequested = false;

enum class Gesture : uint8_t {
    None,
    NextPage,
    Configure,
    ManualRefresh,
};

Gesture gesture = Gesture::None;

void onClick() { gesture = Gesture::NextPage; }
void onDoubleClick() { gesture = Gesture::Configure; }
void onLongPress() { gesture = Gesture::ManualRefresh; }

void migratePreferences() {
    Preferences preferences;
    preferences.begin(PREF_NAMESPACE, false);
    const uint16_t version = preferences.getUShort(PREF_CONFIG_VERSION, 0);
    if (version < 1) {
        if (!preferences.isKey(PREF_ACTIVE_PAGE)) preferences.putUChar(PREF_ACTIVE_PAGE, PAGE_DASHBOARD);
        if (!preferences.isKey(PREF_PAGE_COUNT)) preferences.putUChar(PREF_PAGE_COUNT, PAGE_COUNT);
        if (!preferences.isKey(PREF_POWER_MODE)) preferences.putUChar(PREF_POWER_MODE, 0);
        if (!preferences.isKey(PREF_EXT_MINUTES)) preferences.putUShort(PREF_EXT_MINUTES, 60);
        if (!preferences.isKey(PREF_FOCUS_TEXT)) preferences.putString(PREF_FOCUS_TEXT, "");
        preferences.putUShort(PREF_CONFIG_VERSION, CONFIG_VERSION);
    }
    if (preferences.getUChar(PREF_PAGE_COUNT, PAGE_COUNT) != PAGE_COUNT) {
        preferences.putUChar(PREF_PAGE_COUNT, PAGE_COUNT);
    }
    if (preferences.getUChar(PREF_ACTIVE_PAGE, PAGE_DASHBOARD) >= PAGE_COUNT) {
        preferences.putUChar(PREF_ACTIVE_PAGE, PAGE_DASHBOARD);
    }
    preferences.end();
}

uint8_t activePage() {
    Preferences preferences;
    preferences.begin(PREF_NAMESPACE, true);
    const uint8_t page = preferences.getUChar(PREF_ACTIVE_PAGE, PAGE_DASHBOARD);
    preferences.end();
    return page < PAGE_COUNT ? page : PAGE_DASHBOARD;
}

uint8_t advancePage() {
    Preferences preferences;
    preferences.begin(PREF_NAMESPACE, false);
    const uint8_t page = (preferences.getUChar(PREF_ACTIVE_PAGE, PAGE_DASHBOARD) + 1) % PAGE_COUNT;
    preferences.putUChar(PREF_ACTIVE_PAGE, page);
    preferences.end();
    return page;
}

String configuredFocus() {
    Preferences preferences;
    preferences.begin(PREF_NAMESPACE, true);
    String focus = preferences.getString(PREF_FOCUS_TEXT, "");
    preferences.end();
    if (focus.length() > dashboard::MAX_FOCUS_BYTES) {
        size_t cut = dashboard::MAX_FOCUS_BYTES;
        while (cut > 0 && (static_cast<uint8_t>(focus[cut]) & 0xc0) == 0x80) --cut;
        focus.remove(cut);
    }
    return focus;
}

Gesture detectWakeGesture() {
    const esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();
#if CONFIG_IDF_TARGET_ESP32C3
    if (wakeCause != ESP_SLEEP_WAKEUP_GPIO) return Gesture::None;
#else
    if (wakeCause != ESP_SLEEP_WAKEUP_EXT0) return Gesture::None;
#endif
    gesture = Gesture::None;
    modeButton.setDebounceMs(40);
    modeButton.setClickMs(450);
    modeButton.setPressMs(3000);
    modeButton.attachClick(onClick);
    modeButton.attachDoubleClick(onDoubleClick);
    modeButton.attachLongPressStart(onLongPress);
    const uint32_t started = millis();
    while (gesture == Gesture::None && millis() - started < GESTURE_WINDOW_MS) {
        modeButton.tick();
        delay(10);
    }
    Serial.printf("[Button] gesture=%u\n", static_cast<unsigned>(gesture));
    return gesture;
}

bool manualRefreshAllowed() {
    time_t now = 0;
    time(&now);
    Preferences preferences;
    preferences.begin(PREF_NAMESPACE, false);
    const uint64_t previous = preferences.getULong64(PREF_MANUAL_EPOCH, 0);
    const bool validClock = now >= 1577836800;
    const bool allowed = !validClock || previous == 0 || static_cast<uint64_t>(now) >= previous + 30;
    if (allowed && validClock) preferences.putULong64(PREF_MANUAL_EPOCH, static_cast<uint64_t>(now));
    preferences.end();
    return allowed;
}

String htmlEscape(String value) {
    value.replace("&", "&amp;");
    value.replace("<", "&lt;");
    value.replace(">", "&gt;");
    value.replace("\"", "&quot;");
    value.replace("'", "&#39;");
    return value;
}

void runConfigurationPortal() {
    Preferences preferences;
    preferences.begin(PREF_NAMESPACE, true);
    const uint8_t configuredPower = preferences.getUChar(PREF_POWER_MODE, 0);
    const uint16_t configuredInterval = preferences.getUShort(PREF_EXT_MINUTES, 60);
    preferences.end();
    String pendingSsid;
    String pendingPassword;
    configSaved = false;
    factoryResetRequested = false;

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("J-Calendar-Setup", "JCalendar");
    DNSServer dns;
    dns.start(53, "*", WiFi.softAPIP());
    WebServer server(80);
    server.on("/", HTTP_GET, [&]() {
        const String page = String("<html><meta charset='utf-8'><meta name='viewport' content='width=device-width'>")
            + "<body><h2>J-Calendar 配置</h2><form method='post' action='/save'>"
            + "<label>Wi-Fi 名称（留空则保留）</label><input name='ssid' maxlength='32'><br>"
            + "<label>Wi-Fi 密码</label><input name='password' type='password' maxlength='64'><br>"
            + "<label>今日重点</label><input name='focus' maxlength='144' value='"
            + htmlEscape(configuredFocus()) + "'><br>"
            + "<label>电源模式</label><select name='power'>"
            + "<option value='0'" + (configuredPower == 0 ? " selected" : "") + ">均衡</option>"
            + "<option value='1'" + (configuredPower == 1 ? " selected" : "") + ">省电</option>"
            + "<option value='2'" + (configuredPower == 2 ? " selected" : "") + ">外接电源</option></select><br>"
            + "<label>外接刷新间隔</label><select name='interval'>"
            + "<option value='30'" + (configuredInterval == 30 ? " selected" : "") + ">30 分钟</option>"
            + "<option value='60'" + (configuredInterval != 30 ? " selected" : "") + ">60 分钟</option></select><br>"
            + "<button type='submit'>保存并退出</button></form>"
            + "<p><a href='/factory-confirm'>恢复出厂设置（需要再次确认）</a></p></body></html>";
        server.send(200, "text/html; charset=utf-8", page);
    });
    server.on("/save", HTTP_POST, [&]() {
        String focus = server.arg("focus");
        if (focus.length() > dashboard::MAX_FOCUS_BYTES) {
            size_t cut = dashboard::MAX_FOCUS_BYTES;
            while (cut > 0 && (static_cast<uint8_t>(focus[cut]) & 0xc0) == 0x80) --cut;
            focus.remove(cut);
        }
        const int power = server.arg("power").toInt();
        const int interval = server.arg("interval").toInt();
        Preferences writable;
        writable.begin(PREF_NAMESPACE, false);
        writable.putString(PREF_FOCUS_TEXT, focus);
        writable.putUChar(PREF_POWER_MODE, power >= 0 && power <= 2 ? power : 0);
        writable.putUShort(PREF_EXT_MINUTES, interval == 30 ? 30 : 60);
        writable.end();
        pendingSsid = server.arg("ssid");
        pendingPassword = server.arg("password");
        configSaved = true;
        server.send(200, "text/html; charset=utf-8", "<html><meta charset='utf-8'><body>设置已保存，配置窗口正在关闭。</body></html>");
    });
    server.on("/factory-confirm", HTTP_GET, [&]() {
        server.send(200, "text/html; charset=utf-8",
            "<html><meta charset='utf-8'><body><h2>恢复出厂设置</h2>"
            "<p>此操作会清除设备配置和 Wi-Fi。请输入 RESET 再确认。</p>"
            "<form method='post' action='/factory-reset'><input name='confirm'>"
            "<button type='submit'>确认清除</button></form><a href='/'>取消</a></body></html>");
    });
    server.on("/factory-reset", HTTP_POST, [&]() {
        if (!server.hasArg("confirm") || server.arg("confirm") != "RESET") {
            server.send(400, "text/plain; charset=utf-8", "确认文字不正确，未清除任何设置");
            return;
        }
        factoryResetRequested = true;
        server.send(200, "text/plain; charset=utf-8", "已确认，配置即将清除");
    });
    server.onNotFound([&]() {
        server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
        server.send(302, "text/plain", "");
    });
    server.begin();
    const uint32_t started = millis();
    while (!configSaved && !factoryResetRequested && millis() - started < CONFIG_WINDOW_MS) {
        dns.processNextRequest();
        server.handleClient();
        delay(10);
    }
    server.stop();
    dns.stop();
    WiFi.softAPdisconnect(true);
    if (factoryResetRequested) {
        Preferences writable;
        writable.begin(PREF_NAMESPACE, false);
        writable.clear();
        writable.end();
        wifiManager.resetSettings();
        Serial.println("[Config] factory reset confirmed and completed");
    } else {
        if (configSaved && !pendingSsid.isEmpty()) {
            WiFi.persistent(true);
            WiFi.begin(pendingSsid.c_str(), pendingPassword.c_str());
            WiFi.persistent(false);
        }
        Serial.printf("[Config] portal closed (%s)\n", configSaved ? "saved" : "timeout");
    }
    WiFi.mode(WIFI_STA);
}

uint32_t fnvAppend(uint32_t hash, const String& value) {
    for (size_t i = 0; i < value.length(); ++i) {
        hash ^= static_cast<uint8_t>(value[i]);
        hash *= 16777619UL;
    }
    return hash;
}

uint32_t snapshotDisplayHash(const dashboard::Snapshot& snapshot, bool offline, uint8_t page) {
    uint32_t hash = 2166136261UL;
    hash = fnvAppend(hash, String(page));
    hash = fnvAppend(hash, String(offline));
    hash = fnvAppend(hash, snapshot.localDate);
    hash = fnvAppend(hash, snapshot.weather.locationName);
    hash = fnvAppend(hash, snapshot.weather.condition);
    hash = fnvAppend(hash, String(static_cast<uint8_t>(snapshot.weather.status)));
    if (snapshot.weather.tempC.hasValue) hash = fnvAppend(hash, String(snapshot.weather.tempC.value));
    if (snapshot.weather.tempMinC.hasValue) hash = fnvAppend(hash, String(snapshot.weather.tempMinC.value));
    if (snapshot.weather.tempMaxC.hasValue) hash = fnvAppend(hash, String(snapshot.weather.tempMaxC.value));
    for (uint8_t i = 0; i < snapshot.usageCount; ++i) {
        const dashboard::UsageData& usage = snapshot.usage[i];
        hash = fnvAppend(hash, usage.provider + usage.coverage + String(static_cast<uint8_t>(usage.status)));
        if (usage.inputTokens.hasValue) hash = fnvAppend(hash, String(usage.inputTokens.value));
        if (usage.outputTokens.hasValue) hash = fnvAppend(hash, String(usage.outputTokens.value));
        if (usage.totalTokens.hasValue) hash = fnvAppend(hash, String(usage.totalTokens.value));
        hash = fnvAppend(hash, usage.balance.currency + usage.balance.amount
            + String(static_cast<uint8_t>(usage.balance.status)));
    }
    hash = fnvAppend(hash, snapshot.focus.text);
    return hash;
}

uint32_t calendarDisplayHash() {
    time_t now = 0;
    time(&now);
    struct tm local = {};
    localtime_r(&now, &local);
    char date[16];
    snprintf(date, sizeof(date), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    Preferences preferences;
    preferences.begin(PREF_NAMESPACE, true);
    const String content = String(date) + preferences.getString(PREF_TODO_LIST, "")
        + preferences.getString(PREF_WORD_LIST, "") + preferences.getString(PREF_SI_WEEK_1ST, "0");
    preferences.end();
    return fnvAppend(2166136261UL, content);
}

bool displayChanged(uint8_t page, uint32_t hash, bool force) {
    if (force) return true;
    Preferences preferences;
    preferences.begin(PREF_NAMESPACE, true);
    const uint32_t previous = preferences.getUInt(page == PAGE_DASHBOARD ? PREF_DASH_HASH : PREF_CAL_HASH, 0);
    preferences.end();
    return previous == 0 || previous != hash;
}

void storeDisplayHash(uint8_t page, uint32_t hash) {
    Preferences preferences;
    preferences.begin(PREF_NAMESPACE, false);
    preferences.putUInt(page == PAGE_DASHBOARD ? PREF_DASH_HASH : PREF_CAL_HASH, hash);
    preferences.end();
}

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
#if CONFIG_IDF_TARGET_ESP32C3
        esp_deep_sleep_enable_gpio_wakeup(1ULL << KEY_M, ESP_GPIO_WAKEUP_GPIO_LOW);
#else
        esp_sleep_enable_ext0_wakeup(static_cast<gpio_num_t>(KEY_M), LOW);
#endif
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

void runMainFlow(Gesture wakeGesture) {
    bool forceRefresh = wakeGesture == Gesture::NextPage;
    bool networkAllowed = true;
    if (wakeGesture == Gesture::Configure) {
        runConfigurationPortal();
        forceRefresh = configSaved || factoryResetRequested;
        if (factoryResetRequested) {
            migratePreferences();
        }
    } else if (wakeGesture == Gesture::ManualRefresh) {
        forceRefresh = manualRefreshAllowed();
        networkAllowed = forceRefresh;
        if (!forceRefresh) Serial.println("[Button] manual refresh ignored during 30 second cooldown");
    }

    uint8_t page = activePage();
    if (wakeGesture == Gesture::NextPage) page = advancePage();
    dashboard::Snapshot snapshot;
    const bool cacheAvailable = dashboard::dashboard_cache_load(snapshot);
    bool haveSnapshot = cacheAvailable;
    bool offline = cacheAvailable;
    if (cacheAvailable) Serial.println("[Dashboard] valid cached snapshot loaded");

    WiFi.mode(networkAllowed ? WIFI_STA : WIFI_OFF);
    wifiManager.setHostname("J-Calendar");
    wifiManager.setEnableConfigPortal(false);
    wifiManager.setConnectTimeout(WIFI_TIMEOUT_SECONDS);
    const bool wifiConnected = networkAllowed && wifiManager.autoConnect();
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
            const String focus = configuredFocus();
            if (!focus.isEmpty()) snapshot.focus.text = focus;
            haveSnapshot = true;
            offline = false;
            if (!dashboard::dashboard_cache_store(json.c_str(), json.length())) {
                Serial.println("[Dashboard] cache update failed; continuing with fresh snapshot");
            }
        }
    }

    if (haveSnapshot && page == PAGE_DASHBOARD) {
        const String focus = configuredFocus();
        if (!focus.isEmpty()) snapshot.focus.text = focus;
        const uint32_t hash = snapshotDisplayHash(snapshot, offline, page);
        if (displayChanged(page, hash, forceRefresh)) {
            si_dashboard(snapshot, offline);
            if (waitForScreen()) {
                recordSuccessfulRefresh();
                storeDisplayHash(page, hash);
            }
        } else {
            Serial.println("[Screen] dashboard content unchanged; full refresh skipped");
        }
    } else if (page == PAGE_CALENDAR) {
        const uint32_t hash = calendarDisplayHash();
        if (displayChanged(page, hash, forceRefresh)) {
            si_screen();
            if (waitForScreen()) {
                recordSuccessfulRefresh();
                storeDisplayHash(page, hash);
            }
        } else {
            Serial.println("[Screen] calendar content unchanged; full refresh skipped");
        }
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
    migratePreferences();
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
    runMainFlow(detectWakeGesture());
}
