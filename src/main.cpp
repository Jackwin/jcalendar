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
WiFiManagerParameter para_refresh_mode("refresh_mode", "刷新模式(0全刷/1LUT)", "0", 1); //  屏幕刷新模式

#include <WebServer.h>
WebServer configServer(80);

static String htmlEscape(const String& value) {
    String escaped;
    escaped.reserve(value.length() + 16);
    for (size_t i = 0; i < value.length(); ++i) {
        switch (value.charAt(i)) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        default: escaped += value.charAt(i); break;
        }
    }
    return escaped;
}

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
volatile bool _configRequested = false;
bool _startWebOnConnect = false;
bool _webRunning = false;
void setup() {
    delay(10);
    Serial.begin(115200);
    Serial.println();
    Serial.println("=== J-Calendar ===");
    Serial.printf("Version: %s\r\n", J_VERSION);

    // 清除全刷标记，强制本次全屏刷新
    Preferences p;
    p.begin(PREF_NAMESPACE, false);
    p.putString(PREF_FULL_DATE, "");  // 清空 = 强制全刷
    p.end();

    led_init();
    led_on();
    delay(300);
    led_off();

    // 按键：双击设置标志位，loop 中处理
    button.setClickMs(300);
    button.setPressMs(3000);
    button.attachDoubleClick([]() {
        Serial.println("=== DOUBLE CLICK - will open config portal ===");
        Serial.println("Cloud-service credentials are local-only and cannot be changed from the web page.");
        _configRequested = true;
    });
    attachInterrupt(digitalPinToInterrupt(KEY_M), checkTicks, CHANGE);

    Serial.println("Ready. Web config at http://J-Calendar.local (after WiFi)");
    Serial.println("------------------------------------------");
}

void loop() {
    button.tick();

    // 持续处理 Web 请求（服务器在 WiFi 连接后启动）
    configServer.handleClient();

    // 双击 → 将在 WiFi 连接后自动打开配置页面
    if (_configRequested) {
        _configRequested = false;
        Serial.println("=== DOUBLE CLICK! Web config ready after WiFi ===");
        // 标记：WiFi 连接后自动启动 Web 服务器
        _startWebOnConnect = true;
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
    mainDone = true;

    // 步骤1：连 WiFi
    Serial.println("Step 1: Connecting WiFi...");
    WiFi.mode(WIFI_STA);
    wm.setHostname("J-Calendar");
    wm.setEnableConfigPortal(false);
    wm.setConnectTimeout(10);

    if (wm.autoConnect()) {
        Serial.println("WiFi OK!");

        // WiFi 连上后自动启动 Web 配置服务器
        if (!_webRunning) {
            configServer.on("/", [](){
                Preferences pref;
                pref.begin(PREF_NAMESPACE, true);
                const String todoList = pref.getString(PREF_TODO_LIST, "");
                const String wordList = pref.getString(PREF_WORD_LIST, "");
                pref.end();

                String html = "<html><meta charset='utf-8'><title>J-Calendar</title>";
                html += "<style>body{font-size:1.1em;margin:20px;max-width:680px}textarea{width:100%;min-height:70px;font-size:1em}button{font-size:1em;padding:8px 16px}</style><body>";
                html += "<h2>J-Calendar</h2>";
                html += "<p>Cloud-service credentials are provisioned locally at build time and cannot be viewed or changed from this page.</p>";
                html += "<h3>每日内容</h3><form action='/content' method='POST'>";
                html += "<p>待办（用 | 分隔，屏幕每天轮换一项）</p><textarea name='todo' maxlength='360'>";
                html += htmlEscape(todoList);
                html += "</textarea><p>单词（用 | 分隔，每项格式：word=中文释义）</p><textarea name='words' maxlength='360'>";
                html += htmlEscape(wordList);
                html += "</textarea><p><button type='submit'>保存每日内容</button></p></form>";
                html += "</body></html>";
                configServer.send(200, "text/html", html);
            });
            configServer.on("/content", HTTP_POST, [](){
                if (!configServer.hasArg("todo") || !configServer.hasArg("words")) {
                    configServer.send(400, "text/plain", "Missing content fields");
                    return;
                }
                Preferences pref;
                pref.begin(PREF_NAMESPACE, false);
                pref.putString(PREF_TODO_LIST, configServer.arg("todo").substring(0, 360));
                pref.putString(PREF_WORD_LIST, configServer.arg("words").substring(0, 360));
                pref.end();
                configServer.send(200, "text/html", "<html><meta charset='utf-8'><body><h2>已保存</h2><p>设备将重启并更新每日内容。</p></body></html>");
                delay(500);
                ESP.restart();
            });
            configServer.begin();
            _webRunning = true;
            Serial.printf("=== Web: http://%s/ ===\r\n", WiFi.localIP().toString().c_str());
        }

        // 步骤2：同步时间
        Serial.println("Step 2: Sync SNTP...");
        _sntp_exec();
        while (_sntp_status() == 0) { delay(100); }
        Serial.printf("SNTP status: %d\r\n", _sntp_status());

        // 步骤3：查天气预报和 DeepSeek；OpenAI 用量暂时屏蔽。
        Serial.println("Step 3: Query weather forecast & DeepSeek...");
        weather_exec();
        deepseek_exec();
        while (weather_status() == 0 || deepseek_status() == 0) { delay(100); }
        Serial.printf("Weather: %d  DeepSeek: %d\r\n", weather_status(), deepseek_status());
        if (deepseek_status() == 1) {
            DeepSeekData* ds = deepseek_data();
            Serial.printf("DeepSeek: Flash=%u Pro=%u\r\n",
                          ds->tokensTodayFlash, ds->tokensTodayPro);
        }

        // 步骤4：显示屏幕——强制全刷（调试用）
        Serial.println("Step 4: FULL screen refresh (forced)...");
        si_screen();
        while (si_screen_status() == 0) { delay(100); }
        Serial.printf("Screen status: %d\r\n", si_screen_status());

    } else {
        Serial.println("WiFi FAILED!");
    }

    // 保持 WiFi + Web 30 秒方便配置
    Serial.printf("=== Web: http://%s/ === (30s window)\r\n", WiFi.localIP().toString().c_str());
    unsigned long webStart = millis();
    while (millis() - webStart < 30000) {
        configServer.handleClient();
        delay(10);
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
