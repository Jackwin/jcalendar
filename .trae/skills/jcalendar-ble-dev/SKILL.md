---
name: "jcalendar-ble-dev"
description: "J-Calendar 项目开发指南：涵盖 BLE 数据推送、DeepSeek token 用量显示、GATT 服务设计、深睡眠低功耗、屏幕局部刷新等。当用户为 jcalendar 项目开发新功能或修改现有流程时调用。"
---

# jcalendar-ble-dev

为 J-Calendar（ESP32 墨水屏日历）的综合开发 skill，涵盖 **BLE 数据推送**、**DeepSeek token 用量显示**、屏幕刷新、低功耗管理等核心功能。

## 何时调用本 Skill

- 用户要求为 jcalendar 新增 BLE 推送（天气、日历、倒数日、一言、配置等）功能
- 用户要设计 BLE GATT 服务/特征、或定义手机与设备间的数据协议
- 用户要解决 BLE 与现有深睡眠、Preferences、屏幕刷新流程的集成问题
- 用户要加构建开关、新增 `ble.cpp/ble.h` 源文件，或调整 `platformio.ini`、`main.cpp` 数据流
- 用户要修改 DeepSeek token 用量查询、显示逻辑、每小时刷新策略
- 用户要添加或修改屏幕局部刷新（`si_screen_partial_status()`）、状态栏绘制（`draw_status()`）
- 用户要调整深睡眠唤醒间隔策略（`go_sleep()`）

## 项目背景与关键约束

调用本 skill 前必须理解以下既有架构（勿破坏）：

- **平台**：ESP32 Arduino + PlatformIO，多环境（z21/z15/z98/1680/esp32c3），`SI_DRIVER` 宏区分屏幕驱动
- **配置存储**：[include/_preference.h](file:///d:/workspace/esp32/jcalendar/include/_preference.h) 定义所有 NVS key，namespace=`J_CALENDAR`，key 限 15 字符
- **既有数据流**（[src/main.cpp](file:///d:/workspace/esp32/jcalendar/src/main.cpp)）：
  1. `wm.autoConnect()` 连 WiFi
  2. `_sntp_exec()` 同步时间
  3. `weather_exec()` 经 HTTP 拉取和风天气
  4. `deepseek_exec()` 经 HTTP 调用 DeepSeek Chat + 余额 API
  5. `_sntp_status()>0 && weather_status()>0 && deepseek_status()>0 && si_screen_status()==-1` 时调用 `si_screen()` 全屏刷新
  6. 全屏刷新后记录 `FULL_DATE`（当日已全刷标记）
  7. `go_sleep()` 进入深睡眠（按日/偶数小时/每小时唤醒，取决于配置）
  8. **局部刷新路径**：若今日已全刷 + 定时器唤醒 → 仅 `deepseek_exec_balance_only()` → `si_screen_partial_status()` 局部刷新状态栏（无闪屏）
- **数据结构**：[src/API.hpp](file:///d:/workspace/esp32/jcalendar/src/API.hpp) 定义 `Weather`/`DailyWeather`/`HourlyForecast`/`DailyForecast`/`Hitokoto`/`Bilibili`
- **低功耗**：`go_sleep()` 关闭 WiFi、复位 SPI 引脚、关闭 RTC 存储域；BLE 必须遵循同样的省电纪律
- **按键**：单击刷新、双击配置门户、长按清空 NVS 重启（[src/main.cpp](file:///d:/workspace/esp32/jcalendar/src/main.cpp) L216-329）
- **引脚**（LOLIN32_LITE，[include/wiring.h](file:///d:/workspace/esp32/jcalendar/include/wiring.h)）：KEY_M=GPIO14（RTC 唤醒），未占用 BT 天线相关引脚，BLE 可用

## DeepSeek Token 用量每小时局部刷新（已实现）

### 功能概述
在电子墨水屏状态栏显示 DeepSeek API 的 **今日累计 token 用量** 和 **账户余额**。每日首次唤醒时调用 Chat API（生成"今日寄语"，消耗少量 token）+ 余额 API；之后每小时仅查余额 API（不消耗 token），使用 **局部刷新** 更新状态栏，**避免全屏闪屏**。

### 涉及的源文件

| 文件 | 职责 |
|------|------|
| [include/deepseek.h](file:///d:/workspace/esp32/jcalendar/include/deepseek.h) | `DeepSeekData` 结构体 + `deepseek_exec()` / `deepseek_exec_balance_only()` 声明 |
| [src/deepseek.cpp](file:///d:/workspace/esp32/jcalendar/src/deepseek.cpp) | Chat API（`/chat/completions`）+ 余额 API（`/user/balance`）+ 今日 token 累计（NVS 持久化） |
| [include/_preference.h](file:///d:/workspace/esp32/jcalendar/include/_preference.h) | `PREF_DS_KEY` / `PREF_DS_TODAY` / `PREF_DS_DATE` / `PREF_FULL_DATE` / `PREF_PARTIAL_CNT` |
| [include/screen_ink.h](file:///d:/workspace/esp32/jcalendar/include/screen_ink.h) | `si_screen_partial_status()` 声明 |
| [src/screen_ink.cpp](file:///d:/workspace/esp32/jcalendar/src/screen_ink.cpp) | `draw_status()` 渲染 token/余额/电池 + `si_screen_partial_status()` 局部刷新 + `task_screen()` 写入 `FULL_DATE` |
| [src/main.cpp](file:///d:/workspace/esp32/jcalendar/src/main.cpp) | `loop()` 双路径（全刷 / 局部刷新）+ `go_sleep()` DS 模式下每小时唤醒 |

### 双路径数据流

```
每日首次唤醒（全刷路径）:
  WiFi → SNTP → weather_exec() → deepseek_exec() [Chat+余额]
  → si_screen() 全屏刷新 → 记录 FULL_DATE → go_sleep(1小时)

后续每小时唤醒（局部刷新路径）:
  WiFi → SNTP → deepseek_exec_balance_only() [仅余额,不耗token]
  → si_screen_partial_status() 局部刷新状态栏 → go_sleep(1小时)

每 24 次局部刷新 → 清除 FULL_DATE → 下次自动全刷（防残影）
```

### loop() 路径选择逻辑

```cpp
// 判断条件：今日已全刷 且 定时器唤醒（非按键）
bool todayFullDone = (pref.getString(PREF_FULL_DATE) == todayStr());
esp_sleep_wakeup_cause_t wakeCause = esp_sleep_get_wakeup_cause();

if (todayFullDone && wakeCause == ESP_SLEEP_WAKEUP_TIMER) {
    // → 局部刷新路径：仅 DeepSeek 余额 + 状态栏局部刷新
    deepseek_exec_balance_only();
    si_screen_partial_status();
} else {
    // → 全刷路径：天气 + DeepSeek Chat/余额 + 全屏刷新
    weather_exec();
    deepseek_exec();
    si_screen();
}
```

### DeepSeek API 调用细节

| API | 端点 | 方法 | 频率 | 消耗 token |
|-----|------|------|------|-----------|
| Chat 补全 | `POST /chat/completions` | `callDeepSeekChat()` | 每日 1 次 | ✅ 是（~32 tokens） |
| 账户余额 | `GET /user/balance` | `callDeepSeekBalance()` | 每小时 1 次 | ❌ 否 |

- **模型**：`deepseek-v4-flash`（便宜快速）
- **今日寄语**：生成不超过 15 字，`max_tokens=32`
- **今日累计 token**：通过 `addTodayTokens()` 累加，NVS 跨日自动重置（以 `todayStr()` YYYYMMDD 判断）

### NVS Key 一览（DeepSeek 相关）

| Key | 类型 | 说明 |
|-----|------|------|
| `DS_KEY` | String | DeepSeek API Key（WiFiManager 配置） |
| `DS_TOK_TODAY` | uint32 | 今日累计 token |
| `DS_TOK_DATE` | String | 累计 token 对应日期 YYYYMMDD |
| `FULL_DATE` | String | 今日是否已完成全屏刷新 YYYYMMDD |
| `PARTIAL_CNT` | uint8 | 局部刷新计数（24 次上限） |

### 局部刷新实现细节

```cpp
void si_screen_partial_status() {
    display.init(115200);           // 与全刷相同的初始化
    display.setRotation(ROTATION);
    u8g2Fonts.begin(display);
    draw_status(true);              // partial=true，内部 setPartialWindow
    display.powerOff();
    display.hibernate();
    // 更新 PARTIAL_CNT，>=24 时清除 FULL_DATE 强制下次全刷
}
```

- `draw_status(true)` 内部调用 `display.setPartialWindow(statusX, statusY, statusW, statusH)` 限定 14px 高度区域
- 局部刷新仅重绘状态栏（DeepSeek token + 电池图标），日历/天气区域保持不变
- **同步执行**（非 Task），因为局部刷新速度快（<500ms）

### go_sleep() 唤醒策略

| 条件 | 无 DS Key | 有 DS Key |
|------|----------|----------|
| 每日天气 / 无天气 | 次日唤醒 | **每小时唤醒** |
| 实时天气（奇数小时） | 下个偶数小时 | **每小时唤醒** |
| 实时天气（偶数小时） | 下个偶数小时 | **每小时唤醒** |

### 常见陷阱（DeepSeek 相关）

- **Chat API 消耗 token**：`deepseek_exec()` 仅在每日首次全刷时调用，每小时路径只调 `deepseek_exec_balance_only()`
- **局部刷新 ≠ 无限使用**：e-ink 局部刷新会积累残影，每 24 次（约 1 天）自动触发一次全刷
- **`FULL_DATE` 跨日自动失效**：存储 YYYYMMDD 格式，次日 `todayFullDone` 为 false，自动走全刷路径
- **局部刷新不可用于首次启动**：`FULL_DATE` 为空 → `todayFullDone` 为 false → 走全刷路径
- **`deepseek_exec_balance_only()` 状态初始化**：必须先设 `_deepseek_status = 0`，防止 loop 重复调用

---

## 目标架构：BLE 推送数据流

```
手机（联网）──BLE GATT Write──> ESP32（免联网）
                                 ├─ 解析 JSON/二进制
                                 ├─ 存入 Preferences（复用/扩展 PREF_* key）
                                 ├─ 标记数据新鲜 → 触发 si_screen()
                                 └─ go_sleep()
```

设备侧不再走 `weather_exec()`/`_sntp_exec()`，而是由 BLE 写入回调填充等价数据并推进状态机。

## GATT 服务设计

固定一个主服务，按数据域分特征。UUID 自行生成（16字节），以下为建议命名：

| 特征           | UUID 末段 | 属性                   | 用途                                                             |
| -------------- | --------- | ---------------------- | ---------------------------------------------------------------- |
| Config         | `...-A1`  | Write                  | 下发 WiFi/QWeather/倒数日/Tag/课程表等配置（对应现有 PREF_*）    |
| WeatherNow     | `...-A2`  | Write                  | 推送实时天气（`Weather` 结构）                                   |
| ForecastDaily  | `...-A3`  | Write+WriteWithoutResp | 推送逐天预报（`DailyForecast`，较长，需分包）                    |
| ForecastHourly | `...-A4`  | Write+WriteWithoutResp | 推送逐小时预报（`HourlyForecast`）                               |
| Hitokoto       | `...-A5`  | Write                  | 推送一言                                                         |
| Time           | `...-A6`  | Write                  | 下发当前时间（替代 SNTP，Unix 秒）                               |
| Command        | `...-A7`  | Write                  | 指令：0x01 立即刷新屏幕、0x02 立即休眠、0x03 重启、0x04 清空配置 |
| Status         | `...-A8`  | Read+Notify            | 设备状态：电量mV、屏幕类型、上次更新时间、BLE 连接状态           |

## 分包协议（关键）

BLE 单次 Write 默认 MTU 23（有效 20 字节），协商后可达 ~512。天气 JSON 远超此限，必须分包：

- 方案 A（推荐，简单）：协商 MTU 后，单特征 `Write`（Long Write）最大 512 字节；预报数据拆成多次写，每次写一个 `DailyWeather` 或一组 `HourlyForecast` 元素，特征值末尾追加序号。
- 方案 B（自定义帧）：每包 = `[1B type][2B seq][2B total][N B payload]`，接收端按 seq 缓冲、total 达成后拼装解析。type 复用上表特征编号。
- 建议优先 JSON（与现有 `ArduinoJson` 一致），仅在 RAM 紧张时改二进制。

## 与现有代码的集成规范

### 1. 新增源文件
- [src/ble.cpp](file:///d:/workspace/esp32/jcalendar/src/ble.cpp) / [include/ble.h](file:///d:/workspace/esp32/jcalendar/include/ble.h)：封装 BLE 初始化、广播、GATT 回调、状态机接口
- 对外暴露与既有模块同风格的函数：`ble_init()`、`ble_exec()`、`ble_status()`、`ble_stop()`

### 2. 构建开关
在 [platformio.ini](file:///d:/workspace/esp32/jcalendar/platformio.ini) 各 env 的 `build_flags` 增加：
```
-D ENABLE_BLE
```
未定义时 BLE 代码用 `#ifdef ENABLE_BLE` 屏蔽，保持原有 WiFi 流程不变。

### 3. main.cpp 数据流改造（条件分支）
在 [src/main.cpp](file:///d:/workspace/esp32/jcalendar/src/main.cpp) `loop()` 中，`#ifdef ENABLE_BLE` 分支下：
- 跳过 `_sntp_exec()`/`weather_exec()` 的 WiFi 路径
- 改为 `ble_exec()` 处理已缓冲的 BLE 写入
- 数据就绪后同样调用 `si_screen()`
- `go_sleep()` 前必须 `BLEDevice::deinit()` 并复位蓝牙引脚（与现有 SPI 引脚复位一致）

### 4. 数据落地到 NVS
- 配置类写入复用 [include/_preference.h](file:///d:/workspace/esp32/jcalendar/include/_preference.h) 既有 key
- 天气/一言等"易变数据"需新增 key（保持 ≤15 字符），例如：
  - `W_NOW_JSON`、`W_DAILY_JSON`、`W_HOURLY_JSON`、`HITOKOTO`、`BLE_TIME`
- 新增 key 必须追加到 [include/_preference.h](file:///d:/workspace/esp32/jcalendar/include/_preference.h) 并加注释
- `weather.cpp`/`screen_ink.cpp` 读取数据的入口改为优先读 NVS 缓存（BLE 模式）再回退 HTTP（WiFi 模式）

### 5. 时间同步替代
BLE `Time` 特征下发 Unix 秒后，用 `settimeofday()` 设置系统时间，替代 SNTP；`_sntp_status()` 需在 BLE 模式下返回"已同步"。

## 低功耗与深睡眠集成（必读）

- **BLE 不能跨深睡眠保持**：`go_sleep()` 前 `BLEDevice::deinit()`、`BLEDevice::deinit(false)` 释放内存；唤醒后重新 `ble_init()`
- **广播窗口**：醒后仅广播短窗口（如 60s），超时无连接则直接 `si_screen()`+`go_sleep()`，避免空耗
- **连接间隔**：作为 Peripheral，请求较大 connection interval（如 100~200ms）省电
- **关闭未用域**：沿用 `go_sleep()` 的 `esp_sleep_pd_config(...)` 纪律
- **RAM 预算**：BLE 协议栈约占 30~50KB RAM，注意与 `ArduinoJson` 解析缓冲共用时是否溢出；必要时缩小 `DailyForecast.length`

## 实现步骤建议

1. 在 [platformio.ini](file:///d:/workspace/esp32/jcalendar/platformio.ini) 增加 `-D ENABLE_BLE`，`lib_deps` 无需新增（Arduino-ESP32 自带 `BLEDevice.h`）
2. 新建 [include/ble.h](file:///d:/workspace/esp32/jcalendar/include/ble.h) / [src/ble.cpp](file:///d:/workspace/esp32/jcalendar/src/ble.cpp)，实现 `ble_init/exec/status/stop` 与 GATT 回调骨架
3. 在 [include/_preference.h](file:///d:/workspace/esp32/jcalendar/include/_preference.h) 追加天气缓存 key
4. 用 `#ifdef ENABLE_BLE` 在 [src/main.cpp](file:///d:/workspace/esp32/jcalendar/src/main.cpp) `setup()`/`loop()`/`go_sleep()` 中插入 BLE 分支
5. 实现 `Config` 特征写入 → 复用 `saveParamsCallback` 的存储逻辑
6. 实现 `WeatherNow`/`ForecastDaily`/`Time` 写入 → 存 NVS → 推进状态机 → `si_screen()`
7. 在 `go_sleep()` 加 `ble_stop()` + 引脚复位
8. 用手机调试工具（nRF Connect / LightBlue）验证 GATT 与分包

## 常见陷阱

- **NVS key 超 15 字符**：[include/_preference.h](file:///d:/workspace/esp32/jcalendar/include/_preference.h) 顶部已警告，新增 key 务必检查
- **BLE 写回调里直接刷新屏幕**：GATT 回调在协议栈线程，勿在其中直接调 `si_screen()`（耗时阻塞），应置标志位、在 `loop()` 执行
- **深睡眠前未 deinit**：BLE 持续耗流数 mA，`go_sleep()` 必须先 `BLEDevice::deinit()`
- **双击配置门户与 BLE 冲突**：WiFiManager 与 BLE 同时开可能增加功耗，建议 BLE 模式下双击改为进入 BLE 配置广播而非 WiFi 配置门户
- **esp32c3 环境**：[platformio.ini](file:///d:/workspace/esp32/jcalendar/platformio.ini) `esp32c3` env 的 BLE API 与 ESP32 一致，但 flash 更小，注意分区表 `min_spiffs.csv` 是否够用
- **`weather_status()` 返回值**：[src/main.cpp](file:///d:/workspace/esp32/jcalendar/src/main.cpp) L184 依赖 `>0` 才刷新屏幕，BLE 模式下需让该函数在收到 BLE 数据后返回正值

## 参考文件索引

- 主流程：[src/main.cpp](file:///d:/workspace/esp32/jcalendar/src/main.cpp)
- 数据结构与 HTTP API：[src/API.hpp](file:///d:/workspace/esp32/jcalendar/src/API.hpp)
- 配置 key：[include/_preference.h](file:///d:/workspace/esp32/jcalendar/include/_preference.h)
- 引脚：[include/wiring.h](file:///d:/workspace/esp32/jcalendar/include/wiring.h)
- 构建配置：[platformio.ini](file:///d:/workspace/esp32/jcalendar/platformio.ini)
- 天气拉取：[src/weather.cpp](file:///d:/workspace/esp32/jcalendar/src/weather.cpp)
- 屏幕刷新：[src/screen_ink.cpp](file:///d:/workspace/esp32/jcalendar/src/screen_ink.cpp) / [include/screen_ink.h](file:///d:/workspace/esp32/jcalendar/include/screen_ink.h)
- SNTP：[src/_sntp.cpp](file:///d:/workspace/esp32/jcalendar/src/_sntp.cpp)
- DeepSeek：[src/deepseek.cpp](file:///d:/workspace/esp32/jcalendar/src/deepseek.cpp) / [include/deepseek.h](file:///d:/workspace/esp32/jcalendar/include/deepseek.h)
