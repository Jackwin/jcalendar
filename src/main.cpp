#include <Arduino.h>
#include <ArduinoJson.h>

#include <WiFiManager.h>

#include "esp_sleep.h"

#include <wiring.h>

#include "battery.h"

#include "led.h"
#include "_sntp.h"
#include "weather.h"
#include "screen_ink.h"
#include "_preference.h"
#include "deepseek.h"

#include "version.h"

#include "OneButton.h"
OneButton button(KEY_M, true);

void IRAM_ATTR checkTicks() {
    button.tick();
}

WiFiManager wm;
WiFiManagerParameter para_qweather_host("qweather_host", "和风天气Host", "", 64); //     和风天气key
WiFiManagerParameter para_qweather_key("qweather_key", "和风天气API Key", "", 32); //     和风天气key
// const char* test_html = "<br/><label for='test'>天气模式</label><br/><input type='radio' name='test' value='0' checked> 每日天气test </input><input type='radio' name='test' value='1'> 实时天气test</input>";
// WiFiManagerParameter para_test(test_html);
WiFiManagerParameter para_qweather_type("qweather_type", "天气类型（0:每日天气，1:实时天气）", "0", 2, "pattern='\\[0-1]{1}'"); //     城市code
WiFiManagerParameter para_qweather_location("qweather_loc", "位置ID", "", 64); //     城市code
WiFiManagerParameter para_cd_day_label("cd_day_label", "倒数日（4字以内）", "", 10); //     倒数日
WiFiManagerParameter para_cd_day_date("cd_day_date", "日期（yyyyMMdd）", "", 8, "pattern='\\d{8}'"); //     城市code
WiFiManagerParameter para_tag_days("tag_days", "日期Tag（yyyyMMddx，详见README）", "", 30); //     日期Tag
WiFiManagerParameter para_si_week_1st("si_week_1st", "每周起始（0:周日，1:周一）", "0", 2, "pattern='\\[0-1]{1}'"); //     每周第一天
WiFiManagerParameter para_study_schedule("study_schedule", "课程表", "0", 4000, "pattern='\\[0-9]{3}[;]$'"); //     每周第一天
WiFiManagerParameter para_deepseek_key("deepseek_key", "DeepSeek API Key", "", 64); //     DeepSeek API Key（用于显示token用量/余额）
WiFiManagerParameter para_refresh_mode("refresh_mode", "刷新模式(0全刷/1LUT)", "0", 1); //  屏幕刷新模式

#include <WebServer.h>
WebServer configServer(80);

void print_wakeup_reason() {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
        Serial.println("Wakeup caused by external signal using RTC_IO");
        break;
    case ESP_SLEEP_WAKEUP_EXT1:
    {
        Serial.println("Wakeup caused by external signal using RTC_CNTL");
        uint64_t status = esp_sleep_get_ext1_wakeup_status();
        if (status == 0) {
            Serial.println(" *None of the configured pins woke us up");
        } else {
            Serial.print(" *Wakeup pin mask: ");
            Serial.printf("0x%016llX\r\n", status);
            for (int i = 0; i < 64; i++) {
                if ((status >> i) & 0x1) {
                    Serial.printf("  - GPIO%d\r\n", i);
                }
            }
        }
        break;
    }
    case ESP_SLEEP_WAKEUP_TIMER:
        Serial.println("Wakeup caused by timer");
        break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        Serial.println("Wakeup caused by touchpad");
        break;
    case ESP_SLEEP_WAKEUP_ULP:
        Serial.println("Wakeup caused by ULP program");
        break;
    default:
        Serial.printf("Wakeup was not caused by deep sleep.\r\n");
    }
}

void buttonClick(void* oneButton);
void buttonDoubleClick(void* oneButton);
void buttonLongPressStop(void* oneButton);
void go_sleep();

unsigned long _idle_millis;
unsigned long TIME_TO_SLEEP = 180 * 1000;

bool _wifi_flag = false;
unsigned long _wifi_failed_millis;
volatile bool _configRequested = false;  // 双击标志位
void setup() {
    delay(10);
    Serial.begin(115200);
    Serial.println();
    Serial.println("=== J-Calendar Minimal Debug ===");
    Serial.printf("Version: %s\r\n", J_VERSION);

    led_init();
    led_on();
    delay(300);
    led_off();

    // 按键：双击设置标志位，loop 中处理
    button.setClickMs(300);
    button.setPressMs(3000);
    button.attachDoubleClick([]() {
        Preferences p;
        p.begin(PREF_NAMESPACE, true);
        String savedKey = p.getString(PREF_DS_KEY, "");
        p.end();
        Serial.println("=== DOUBLE CLICK - will open config portal ===");
        Serial.print("Saved DS Key: ");
        Serial.println(savedKey.length() > 0 ? savedKey.substring(0,8) + "..." : "(empty)");
        _configRequested = true;
    });
    attachInterrupt(digitalPinToInterrupt(KEY_M), checkTicks, CHANGE);
    
    Serial.println("Ready. Double-click button to configure DeepSeek Key.");
    Serial.println("------------------------------------------");
}

void loop() {
    button.tick();
    wm.process();
    
    // 双击触发 → 打开网页配置（ESP32 WebServer，任何时间都能访问）
    if (_configRequested) {
        _configRequested = false;
        Serial.println("=== Opening Web Config ===");
        Serial.printf("Visit http://%s/ to configure DeepSeek Key\r\n", WiFi.localIP().toString().c_str());
        
        // 设置 Web 路由
        configServer.on("/", [](){
            String html = "<html><head><meta charset='utf-8'><title>J-Calendar</title></head><body>";
            html += "<h2>J-Calendar 配置</h2>";
            html += "<form action='/save' method='POST'>";
            html += "DeepSeek API Key: <input type='text' name='key' size='40' value='";
            Preferences p;
            p.begin(PREF_NAMESPACE, true);
            html += p.getString(PREF_DS_KEY, "");
            p.end();
            html += "'><br><br>";
            html += "<input type='submit' value='保存'>";
            html += "</form></body></html>";
            configServer.send(200, "text/html", html);
        });
        
        configServer.on("/save", [](){
            if (configServer.hasArg("key")) {
                String key = configServer.arg("key");
                Preferences p;
                p.begin(PREF_NAMESPACE, false);
                p.putString(PREF_DS_KEY, key);
                p.end();
                configServer.send(200, "text/html", 
                    "<html><meta charset='utf-8'><body>"
                    "<h2>✅ Key 已保存！</h2>"
                    "<p>设备将在 3 秒后重启...</p>"
                    "</body></html>");
                Serial.println("[Config] Key saved via web! Restarting...");
                delay(3000);
                ESP.restart();
            } else {
                configServer.send(400, "text/html", "Missing key parameter");
            }
        });
        
        configServer.begin();
        Serial.println("[Config] Web server started on port 80");
        Serial.printf("[Config] Visit http://%s/\r\n", WiFi.localIP().toString().c_str());
        
        // 运行 Web 服务器 2 分钟
        led_config();
        unsigned long configStart = millis();
        while (millis() - configStart < 120000) {
            configServer.handleClient();
            button.tick();
            delay(10);
        }
        configServer.stop();
        Serial.println("[Config] Web server stopped");
        led_on();
        return;
    }
    
    // 配置门户运行中，不执行主流程
    if (wm.getConfigPortalActive()) {
        delay(10);
        return;
    }
    
    // 主流程：只执行一次
    static bool mainDone = false;
    if (mainDone) {
        delay(10);
        return;
    }
    
    // 启动后等待 20 秒，方便用户双击进入配置
    static unsigned long bootTime = millis();
    if (millis() - bootTime < 20000) {
        if (millis() - bootTime < 100) {
            Serial.println("------------------------------------------");
            Serial.println("[INFO] Double-click button to open Web Config.");
            Serial.printf("[INFO] 20 seconds countdown...\r\n");
        }
        if ((millis() - bootTime) % 5000 < 15) {
            Serial.printf("[%lu] Double-click for config...\r\n", 20 - (millis() - bootTime) / 1000);
        }
        delay(10);
        return;
    }
    
    mainDone = true;
    
    // 步骤1：连 WiFi
    Serial.println("Step 1: Connecting WiFi...");
    WiFi.mode(WIFI_STA);
    wm.setHostname("J-Calendar");
    wm.setEnableConfigPortal(false);
    wm.setConnectTimeout(10);
    
    if (wm.autoConnect()) {
        Serial.println("WiFi OK!");
        
        // 步骤2：同步时间
        Serial.println("Step 2: Sync SNTP...");
        _sntp_exec();
        while (_sntp_status() == 0) { delay(100); }
        Serial.printf("SNTP status: %d\r\n", _sntp_status());
        
        // 步骤3：查 DeepSeek 余额（仅余额，不消耗 token）
        Serial.println("Step 3: Query DeepSeek balance...");
        deepseek_exec_balance_only();
        while (deepseek_status() == 0) { delay(100); }
        Serial.printf("DeepSeek status: %d\r\n", deepseek_status());
        
        if (deepseek_status() == 1) {
            DeepSeekData* ds = deepseek_data();
            Serial.printf("Balance: %s %s\r\n", ds->currency.c_str(), ds->totalBalance.c_str());
            Serial.printf("Today: Flash=%u Pro=%u\r\n", ds->tokensTodayFlash, ds->tokensTodayPro);
        }
        
        // 步骤4：显示屏幕
        // 首次启动：全刷。后续唤醒：仅局部刷新状态栏（DeepSeek数据变化时才刷）
        time_t now = 0;
        time(&now);
        struct tm t;
        localtime_r(&now, &t);
        char todayBuf[9];
        snprintf(todayBuf, sizeof(todayBuf), "%04d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
        
        Preferences pref;
        pref.begin(PREF_NAMESPACE, true);
        String fullDate = pref.getString(PREF_FULL_DATE, "");
        bool todayFullDone = (fullDate == String(todayBuf));
        pref.end();
        
        if (!todayFullDone) {
            // 今日首次 = 全刷
            Serial.println("Step 4: FULL screen refresh...");
            si_screen();
        } else {
            // 今日非首次 = 局部刷新状态栏（免闪屏）
            Serial.println("Step 4: PARTIAL status refresh (no flicker)...");
            si_screen_partial_status();
        }
        while (si_screen_status() == 0) { delay(100); }
        Serial.printf("Screen status: %d\r\n", si_screen_status());
        
    } else {
        Serial.println("WiFi FAILED!");
    }
    
    WiFi.mode(WIFI_OFF);
    
    // 计算下次唤醒时间：每小时整点
    time_t now2 = 0;
    time(&now2);
    struct tm local;
    localtime_r(&now2, &local);
    int secsToNextHour = (60 - local.tm_min) * 60 - local.tm_sec + 10;
    
    Serial.printf("Deep sleep %ds until next hour...\r\n", secsToNextHour);
    delay(100);
    
    esp_sleep_enable_timer_wakeup((uint64_t)secsToNextHour * 1000000ULL);
    esp_sleep_enable_ext0_wakeup(KEY_M, LOW);
    esp_deep_sleep_start();
}

// ====== 以下旧代码临时禁用 ======
#if 0


// 刷新页面
void buttonClick(void* oneButton) {
    Serial.println("Button click.");
    if (wm.getConfigPortalActive()) {
        Serial.println("In config status.");
    } else {
        Serial.println("Refresh screen manually.");
        Preferences pref;
        pref.begin(PREF_NAMESPACE);
        int _si_type = pref.getInt(PREF_SI_TYPE);
        pref.putInt(PREF_SI_TYPE, _si_type == 0 ? 1 : 0);
        pref.end();
        si_screen();
    }
}

void saveParamsCallback() {
    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    pref.putString(PREF_QWEATHER_HOST, para_qweather_host.getValue());
    pref.putString(PREF_QWEATHER_KEY, para_qweather_key.getValue());
    pref.putString(PREF_QWEATHER_TYPE, strcmp(para_qweather_type.getValue(), "1") == 0 ? "1" : "0");
    pref.putString(PREF_QWEATHER_LOC, para_qweather_location.getValue());
    pref.putString(PREF_CD_DAY_LABLE, para_cd_day_label.getValue());
    pref.putString(PREF_CD_DAY_DATE, para_cd_day_date.getValue());
    pref.putString(PREF_TAG_DAYS, para_tag_days.getValue());
    pref.putString(PREF_SI_WEEK_1ST, strcmp(para_si_week_1st.getValue(), "1") == 0 ? "1" : "0");
    pref.putString(PREF_STUDY_SCHEDULE, para_study_schedule.getValue());
    pref.putString(PREF_DS_KEY, para_deepseek_key.getValue());
    pref.putUChar(PREF_REFRESH_MODE, strcmp(para_refresh_mode.getValue(), "1") == 0 ? 1 : 0);
    pref.end();

    Serial.println("Params saved.");

    _idle_millis = millis(); // 刷新无操作时间点

    ESP.restart();
}

void preSaveParamsCallback() {
}

// 双击打开配置页面
void buttonDoubleClick(void* oneButton) {
    Serial.println("Button double click.");
    if (wm.getConfigPortalActive()) {
        ESP.restart();
        return;
    }

    if (weather_status == 0) {
        weather_stop();
    }
    deepseek_stop();

    // 设置配置页面
    // 根据配置信息设置默认值
    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    String qHost = pref.getString(PREF_QWEATHER_HOST);
    String qToken = pref.getString(PREF_QWEATHER_KEY);
    String qType = pref.getString(PREF_QWEATHER_TYPE, "0");
    String qLoc = pref.getString(PREF_QWEATHER_LOC);
    String cddLabel = pref.getString(PREF_CD_DAY_LABLE);
    String cddDate = pref.getString(PREF_CD_DAY_DATE);
    String tagDays = pref.getString(PREF_TAG_DAYS);
    String week1st = pref.getString(PREF_SI_WEEK_1ST, "0");
    String studySchedule = pref.getString(PREF_STUDY_SCHEDULE);
    String dsKey = pref.getString(PREF_DS_KEY);
    String refreshMode = pref.getUChar(PREF_REFRESH_MODE, 0) == 1 ? "1" : "0";
    pref.end();

    para_qweather_host.setValue(qHost.c_str(), 64);
    para_qweather_key.setValue(qToken.c_str(), 32);
    para_qweather_location.setValue(qLoc.c_str(), 64);
    para_qweather_type.setValue(qType.c_str(), 1);
    para_cd_day_label.setValue(cddLabel.c_str(), 16);
    para_cd_day_date.setValue(cddDate.c_str(), 8);
    para_tag_days.setValue(tagDays.c_str(), 30);
    para_si_week_1st.setValue(week1st.c_str(), 1);
    para_study_schedule.setValue(studySchedule.c_str(), 4000);
    para_deepseek_key.setValue(dsKey.c_str(), 64);
    para_refresh_mode.setValue(refreshMode.c_str(), 1);

    wm.setTitle("J-Calendar");
    wm.addParameter(&para_si_week_1st);
    wm.addParameter(&para_qweather_host);
    wm.addParameter(&para_qweather_key);
    wm.addParameter(&para_qweather_type);
    wm.addParameter(&para_qweather_location);
    wm.addParameter(&para_cd_day_label);
    wm.addParameter(&para_cd_day_date);
    wm.addParameter(&para_tag_days);
    wm.addParameter(&para_study_schedule);
    wm.addParameter(&para_deepseek_key);
    wm.addParameter(&para_refresh_mode);
    // std::vector<const char *> menu = {"wifi","wifinoscan","info","param","custom","close","sep","erase","update","restart","exit"};
    std::vector<const char*> menu = { "wifi","param","update","sep","info","restart","exit" };
    wm.setMenu(menu); // custom menu, pass vector
    wm.setConfigPortalBlocking(false);
    wm.setBreakAfterConfig(true);
    wm.setPreSaveParamsCallback(preSaveParamsCallback);
    wm.setSaveParamsCallback(saveParamsCallback);
    wm.setSaveConnect(false); // 保存完wifi信息后是否自动连接，设置为否，以便于用户继续配置param。
    wm.startConfigPortal("J-Calendar", "password");

    led_config(); // LED 进入三快闪状态

    // 控制配置超时180秒后休眠
    _idle_millis = millis();
}

// 重置系统，并重启
void buttonLongPressStop(void* oneButton) {
    Serial.println("Button long press.");

    // 删除Preferences，namespace下所有健值对。
    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    pref.clear();
    pref.end();

    ESP.restart();
}

#define uS_TO_S_FACTOR 1000000
#define TIMEOUT_TO_SLEEP  10 // seconds
time_t blankTime = 0;
void go_sleep() {
    uint64_t p;
    // 根据配置情况来刷新，如果未配置qweather信息，则24小时刷新，否则每2小时刷新
    // 如果配置了 DeepSeek Key，则始终每小时刷新（方便查看 token 用量）
    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    String _qweather_key = pref.getString(PREF_QWEATHER_KEY, "");
    String _ds_key = pref.getString(PREF_DS_KEY, "");
    pref.end();

    time_t now;
    time(&now);
    struct tm local;
    localtime_r(&now, &local);

    bool dsConfigured = (_ds_key.length() > 0);

    if (_qweather_key.length() == 0 || weather_type() == 0) { // 没有配置天气或者使用按日天气，则第二天刷新。
        if (dsConfigured) {
            // DeepSeek 已配置：每小时唤醒一次更新 token 用量
            int secondsToNextHour = (60 - local.tm_min) * 60 - local.tm_sec;
            Serial.printf("Seconds to next hour (DS mode): %d seconds.\n", secondsToNextHour);
            p = (uint64_t)(secondsToNextHour);
            p = p < 0 ? 3600 : (p + 10);
        } else {
            // Sleep to next day
            int secondsToNextDay = (24 - local.tm_hour) * 3600 - local.tm_min * 60 - local.tm_sec;
            Serial.printf("Seconds to next day: %d seconds.\n", secondsToNextDay);
            p = (uint64_t)(secondsToNextDay);
            p = p < 0 ? 3600 * 24 : (p + 30); // 额外增加30秒，避免过早唤醒
        }
    } else {
        // 实时天气：每偶数小时刷新
        if (dsConfigured) {
            // DeepSeek 已配置：每小时唤醒
            int secondsToNextHour = (60 - local.tm_min) * 60 - local.tm_sec;
            Serial.printf("Seconds to next hour (DS+weather mode): %d seconds.\n", secondsToNextHour);
            p = (uint64_t)(secondsToNextHour);
            p = p < 0 ? 3600 : (p + 10);
        } else {
            // Sleep to next even hour.
            int secondsToNextHour = (60 - local.tm_min) * 60 - local.tm_sec;
            if ((local.tm_hour % 2) == 0) { // 如果是奇数点，则多睡1小时
                secondsToNextHour += 3600;
            }
            Serial.printf("Seconds to next even hour: %d seconds.\n", secondsToNextHour);
            p = (uint64_t)(secondsToNextHour);
            p = p < 0 ? 3600 : (p + 10); // 额外增加10秒，避免过早唤醒
        }
    }

    esp_sleep_enable_timer_wakeup(p * (uint64_t)uS_TO_S_FACTOR);
    esp_sleep_enable_ext0_wakeup(KEY_M, LOW);

    // 省电考虑，关闭RTC外设和存储器
    // esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF); // RTC IO, sensors and ULP, 注意：由于需要按键唤醒，所以不能关闭，否则会导致RTC_IO唤醒(ext0)失败
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF); // 
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC8M, ESP_PD_OPTION_OFF);

    gpio_deep_sleep_hold_dis(); // 解除所有引脚的保持状态
    
    // 省电考虑，重置gpio，平均每针脚能省8ua。
    // gpio_reset_pin(PIN_LED_R); // 减小deep-sleep电流
    gpio_reset_pin(SPI_CS); // 减小deep-sleep电流
    gpio_reset_pin(SPI_DC); // 减小deep-sleep电流
    gpio_reset_pin(SPI_RST); // 减小deep-sleep电流
    gpio_reset_pin(SPI_BUSY); // 减小deep-sleep电流`
    gpio_reset_pin(SPI_MOSI); // 减小deep-sleep电流
    gpio_reset_pin(SPI_MISO); // 减小deep-sleep电流
    gpio_reset_pin(SPI_SCK); // 减小deep-sleep电流
    // gpio_reset_pin(PIN_ADC); // 减小deep-sleep电流
    // gpio_reset_pin(I2C_SDA); // 减小deep-sleep电流
    // gpio_reset_pin(I2C_SCL); // 减小deep-sleep电流

    delay(10);
    Serial.println("Deep sleep...");
    Serial.flush();
    esp_deep_sleep_start();
}

#endif // #if 0