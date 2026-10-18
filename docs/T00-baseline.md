# T00 基线审计

审计日期：2026-10-18  
分支：`feat/openai-usage-calendar-panel`  
基线提交：`27beb87 feat: add weather dashboard and configuration`

## 构建基线

命令：

```sh
/Users/wangchunjie.jack/.platformio/penv/bin/pio run -e z21
```

首次构建暴露出两个未声明的既有依赖：`ArduinoUZlib.h` 和 `nongli.h`。本任务已在
`platformio.ini` 恢复显式依赖声明，随后构建成功。

构建摘要：

```text
Environment: z21
Platform: Espressif 32 7.1.3
Board: ESP32 Dev Module, 240 MHz, 320 KiB RAM, 4 MiB flash
RAM:   82,208 / 327,680 bytes (25.1%)
Flash: 1,656,841 / 1,966,080 bytes (84.3%)
Result: SUCCESS (55.53 seconds, cold dependency build)
```

完整构建产物位于本地 `.pio/build/z21/`；该目录按项目规则不提交。

## 构建目标与屏幕驱动

| PlatformIO 环境 | `SI_DRIVER` | 驱动类 | 分辨率/颜色 |
|---|---:|---|---|
| `z21`（默认） | 21 | `GxEPD2_420c_Z21` | 400×300，黑白红三色 |
| `z15` | 15 | `GxEPD2_420c` | 400×300，黑白红三色 |
| `z98` | 98 | `GxEPD2_420c_GDEY042Z98` | 400×300，黑白红三色 |
| `1680` | 1680 | `GxEPD2_420c_1680` | 400×300，黑白红三色 |
| `esp32c3` | 98 | `GxEPD2_420c_GDEY042Z98` | 400×300，黑白红三色 |

固件选择与项目默认值均为 `z21`，但仓库信息不能证明用户手中实物的屏幕丝印；在实物
确认前不得把默认值描述为已验证型号。T00 仅验证默认 `z21` 的编译，其余环境留待 T09
统一回归。

## Flash 分区

`min_spiffs.csv` 面向 4 MiB flash：

| 分区 | Offset | Size |
|---|---:|---:|
| NVS | `0x9000` | `0x5000`（20 KiB） |
| OTA data | `0xe000` | `0x2000`（8 KiB） |
| App OTA 0 | `0x10000` | `0x1e0000`（1,966,080 B） |
| App OTA 1 | `0x1f0000` | `0x1e0000`（1,966,080 B） |
| SPIFFS | `0x3d0000` | `0x20000`（128 KiB） |
| Coredump | `0x3f0000` | `0x10000`（64 KiB） |

当前固件占单个 app 分区 84.3%，后续模块需持续检查 flash 余量。SPIFFS 只有 128 KiB，
实现双槽缓存前必须先限制快照大小并验证实际文件系统开销。

## 已确认代码路径

- `src/main.cpp` 当前启动时清空 `FULL_DATE`，并强制全刷；这是调试行为，T07 移除。
- 当前联网成功后同步 SNTP，随后同时调用天气和 `deepseek_exec()`；OpenAI 客户端存在但未接主流程。
- `deepseek_exec()` 会调用两个 chat completion 并累计本机请求 token；T02 必须禁用该自动调用，且旧累计值不得作为账户用量迁移。
- Web 服务在普通启动后持续开启，与 180 秒配置窗口要求不符；T07/T08 收敛。
- 局刷函数和配置键存在，但默认能力仍按“仅全刷”处理，等待实物确认。

## 待硬件验证

- 实物屏幕丝印和控制器是否确为 Z21。
- BUSY 引脚的实际全刷最长耗时与异常超时边界。
- 局刷、红层变化和长时间残影表现；未验证前不启用局刷。
- 电池 ADC 是否实际接线、负载下电压曲线和低电阈值。
- 72 小时运行、唤醒次数、Wi-Fi/屏刷/休眠电流积分和可用电池容量。

