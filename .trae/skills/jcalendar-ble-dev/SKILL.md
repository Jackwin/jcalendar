---
name: jcalendar-ble-dev
description: J-Calendar ESP32 墨水屏日历固件开发指南，涵盖 400x300 三色屏、农历与节气、OpenAI/DeepSeek Token 统计、安全代理、PlatformIO 编译烧录、BLE 和深度睡眠。用于修改、调试、构建或部署本项目时。
---

# J-Calendar 固件开发

## 工作原则

- 在 `D:\workspace\esp32\jcalendar` 内工作。
- 修改前检查工作树，保留用户已有变更，不重置或覆盖无关文件。
- 优先使用 `rg`、`rg --files` 定位实现。
- 修改后执行与风险相称的构建或校验。
- 未经用户明确要求，不改变已确认的显示布局、颜色和信息层级。

## 当前产品基线

项目使用 ESP32 Arduino 和 PlatformIO。默认目标环境是 `z21`，其他硬件环境以 `platformio.ini` 为准。

典型启动流程：

1. 初始化硬件和网络。
2. 通过 SNTP 校时。
3. 获取天气、OpenAI 用量和 DeepSeek 用量；各数据源独立容错。
4. 一次性完成墨水屏全屏刷新。
5. 释放网络、BLE 等资源后进入深度睡眠。

### 已确认的显示布局

这是用户认可的默认方案，除非用户明确要求，否则必须保留：

- 屏幕为 400 × 300 三色电子墨水屏。
- `Y=0..189` 为日历区：月份、天气摘要、每日待办、英文单词、星期、阳历日期、农历、节气和节日。
- 周末使用红色；当天使用红色圆角底色突出。
- 节气和节日优先于普通农历日期显示。
- `Y=190..299` 为 Token 区，左右两个简洁卡片。
- 左侧 OpenAI：红色标题，显示 UTC 当日总量、输入、输出和日期。
- 右侧 DeepSeek：黑色标题，显示当日总量、Flash 和 Pro。
- 任一服务失败不得妨碍另一个服务或日历显示。
- 数值为零时保持信息可读，进度条显示为空，不伪造占比。

显示相关实现主要位于 `src/screen_ink.cpp`：

- `CALENDAR_PANEL_HEIGHT`
- `compactLunarLabel`
- `draw_compact_calendar`
- `draw_deepseek_token_panel`（历史函数名，实际绘制双服务 Token 面板）

调整布局时先核对实际坐标、字体高度和卡片边界，避免文字重叠或三色屏残影。

## 日历与农历

- 使用项目已有 `si_calendar` 逻辑，不另引入大型日期库。
- 关注 `nl_month_days`、`lunarDates[31]`、`nl_year_jq` 和 `jqAccDate`。
- 日期数组按 `day - 1` 索引；闰月等特殊值保持现有符号约定。
- 中文农历优先使用项目内最小可读中文字体（如 `wqy12`）。
- 当前日历单元格高度约 22 px；阳历可使用约 11 px 字体，农历使用约 12 px 字体。
- 同一天存在节气、节日和普通农历文本时，按产品现有优先级选择一个短标签，避免拥挤。

## 每日信息条

- 使用 `daily_brief_for_day(tm_yday)` 为当天选择一个待办和一个英文词；内容按日期稳定轮换。
- `TODO_LIST` 使用 `|` 分隔短待办；`WORD_LIST` 使用 `|` 分隔 `word=中文释义`。
- 两个 key 均在 NVS 15 字符限制内。列表为空时使用内置默认内容。
- 本地网页只允许编辑这些非敏感展示文本；不得扩展为读取、显示或写入任何云服务密钥。
- 显示前按字体实际宽度裁剪 UTF-8 文本，避免与天气或单词区域重叠。

## OpenAI Token 用量

### 安全架构

绝不把 OpenAI 组织管理员密钥写入 ESP32 固件、NVS、网页配置、日志或 Git 仓库。

采用以下链路：

```text
ESP32 --HTTPS + 设备令牌--> Cloudflare Worker
Worker --管理员密钥--> OpenAI Usage API
Worker --精简 JSON--> ESP32
```

设备只持有可单独撤销的 `DEVICE_TOKEN`。代理负责保存管理员密钥、查询 UTC 当日数据、聚合输入和输出 Token，并返回固件所需的最小 JSON。

相关文件通常包括：

- `include/openai_usage.h`
- `src/openai_usage.cpp`
- 私有配置示例头文件
- `proxy/openai_usage_proxy.js`
- `proxy/wrangler.toml` 或等价 Wrangler 配置

实现要求：

- HTTPS 客户端使用 `setCACert` 校验证书，禁止 `setInsecure()`。
- 私有配置文件加入 `.gitignore`；仅提交不含真实值的示例。
- 缺少私有头文件时仍应能够编译，只把 OpenAI 数据标记为不可用。
- “当日”统一按 UTC 统计，并在屏幕上明确标注 UTC，避免本地时区造成口径误解。
- 不在串口输出密钥、完整授权头或包含密钥的 URL。

### Windows 部署代理

在安装 Node.js 后，通过 PowerShell 执行：

```powershell
Set-Location D:\workspace\esp32\jcalendar\proxy
npm init -y
npm install --save-dev wrangler@latest
npx.cmd wrangler login
npx.cmd wrangler secret put OPENAI_ADMIN_KEY
npx.cmd wrangler secret put DEVICE_TOKEN
npx.cmd wrangler deploy
```

将部署后的 Worker URL 和设备令牌写入本地私有配置，不提交真实值。更新 Secret 时再次执行 `wrangler secret put`，不要写入 `wrangler.toml`。

## DeepSeek Token 用量

- 保留项目已有 DeepSeek 请求与每日累计逻辑，相关实现以 `deepseek_exec` 和对应头文件为入口。
- DeepSeek 与 OpenAI 同时启动并独立更新状态；一个请求失败不能清空另一个服务的有效结果。
- `DEEPSEEK_API_KEY` 可来自被忽略的私有头文件，并优先于旧版 NVS `DS_KEY`。
- 当日累计数据可保存在 NVS，但不得通过未认证的配置页面读出或写入真实 API 密钥。
- 修改字段或模型拆分时同步检查显示层、JSON 解析和持久化结构。

## 密钥与配置约束

- ESP32 NVS namespace 和 key 名长度受限；key 最长 15 个字符。
- 未认证 HTTP 配置接口不得接收、回显或保存任何云服务密钥。
- 示例文件使用清晰的占位符，禁止出现可用凭证。
- 量产设备建议启用 Flash Encryption、Secure Boot，并为每台设备分配独立令牌。
- 设备令牌泄露时应能只撤销该设备，不影响 OpenAI 管理员密钥。

## PlatformIO 构建与烧录（Windows）

若终端无法识别 `pio`，直接使用项目开发机上的完整路径：

```powershell
& 'C:\Users\rcwcj\.platformio\penv\Scripts\pio.exe' run -e z21 -j 1
```

烧录前连接开发板并在设备管理器确认串口，例如 `COM5`：

```powershell
& 'C:\Users\rcwcj\.platformio\penv\Scripts\pio.exe' run -e z21 -t upload --upload-port COM5
```

查看串口日志：

```powershell
& 'C:\Users\rcwcj\.platformio\penv\Scripts\pio.exe' device monitor --port COM5 --baud 115200
```

如果上传阶段一直等待连接：

1. 按住开发板 `BOOT`。
2. 短按 `EN/RESET`，或在开始连接时保持 `BOOT`。
3. 看到写入进度后松开 `BOOT`。
4. 烧录完成后按一次 `EN/RESET`。

不要猜测串口号。先用以下命令确认：

```powershell
& 'C:\Users\rcwcj\.platformio\penv\Scripts\pio.exe' device list
```

## BLE 与内存

- BLE 功能保持在独立源文件中，并使用构建宏控制，避免不需要时增加固件体积和 RAM。
- BLE 回调只解析、入队或更新状态，不在回调中刷新墨水屏或执行长时间网络请求。
- 数据超过单包长度时使用明确的分片、序号、长度和校验规则。
- 尽量复用已有数据结构和缓冲区，避免在 ESP32 上频繁分配大块堆内存。
- 进入深度睡眠前停止广播、断开连接并释放 BLE/Wi-Fi 资源。
- 每次引入 BLE、TLS、字体或大型 JSON 后检查 PlatformIO 报告的 RAM 与 Flash 占用。

## 常用定位顺序

1. 查看 `platformio.ini` 确认环境、宏和依赖。
2. 查看 `src/main.cpp` 理解启动、联网、刷新和睡眠流程。
3. 查看 `src/screen_ink.cpp` 与对应头文件处理布局。
4. 查看 OpenAI、DeepSeek 和天气模块确认请求与状态边界。
5. 查看 `.gitignore` 和私有配置示例，确认凭证不会进入版本控制。
6. 修改后至少构建目标环境；涉及硬件交互时再烧录并检查串口日志。

## 完成检查

- 已保留用户确认的日历与双 Token 卡片布局。
- 农历、节气、节日和当日高亮无重叠。
- OpenAI 总量等于输入与输出的统计口径，日期标明 UTC。
- DeepSeek 总量及 Flash/Pro 拆分可独立显示。
- 任一 API 失败时页面仍完整，另一服务不受影响。
- 固件和代理中没有真实管理员密钥，日志不泄露凭证。
- `z21` 构建成功；需要烧录时确认了正确 COM 端口。
