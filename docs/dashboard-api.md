# J-Calendar Dashboard API v1

## 请求

```http
GET /v1/dashboard HTTP/1.1
Authorization: Bearer <device-read-token>
Accept: application/json
```

只接受 HTTPS。设备令牌只允许读取已绑定设备的快照；位置、OpenAI 组织/项目范围和
DeepSeek 数据源由服务端设备配置决定，客户端不能传任意上游 URL。供应商密钥绝不返回
给设备。成功响应使用 `Cache-Control: private, no-store`，响应体不得超过 8 KiB。

## 顶层字段

| 字段 | 类型 | 规则 |
|---|---|---|
| `schema_version` | integer | 当前为 `1`；设备拒绝未知主版本 |
| `generated_at` | RFC 3339 string | 快照内容生成时间；内容未变时不应仅为刷新时间而改变 |
| `timezone` | IANA zone string | 默认 `Asia/Shanghai` |
| `local_date` | `YYYY-MM-DD` | `timezone` 下快照所属日期 |
| `weather` | object | 天气模块，失败不影响 usage |
| `usage` | array | 最多两个主页供应商条目 |
| `focus` | object | 单条今日重点 |
| `refresh_after_seconds` | integer | 建议下次拉取间隔，不覆盖固件安全下限 |

`status` 枚举为 `ok`、`stale`、`not_configured`、`unsupported`、`error`、`unknown`。
未知或不可用值必须是 JSON `null`，不能用 `0` 替代。时间字段采用 RFC 3339 UTC 时间；
源没有提供观测时间时 `observed_at` 为 `null`。

## 天气

```json
{
  "status": "ok",
  "source": "qweather",
  "observed_at": "2026-10-18T00:50:00Z",
  "fetched_at": "2026-10-18T01:00:00Z",
  "location_name": "北京",
  "temp_c": 21,
  "temp_min_c": 14,
  "temp_max_c": 23,
  "condition": "晴",
  "condition_code": "100"
}
```

温度为有符号整数摄氏度。缺当前观测时 `temp_c` 可为 `null`；若高低温可用，固件改显
示日范围。`observed_at` 超过两小时后服务端标 `stale`，超过六小时不得继续描述为当前天气。

## 用量与余额

```json
{
  "provider": "openai",
  "status": "ok",
  "source": "provider_api",
  "coverage": "project",
  "completeness": "complete",
  "window_start": "2026-10-17T16:00:00Z",
  "window_end": "2026-10-18T01:00:00Z",
  "fetched_at": "2026-10-18T01:00:00Z",
  "observed_at": null,
  "input_tokens": 1020000,
  "output_tokens": 260000,
  "total_tokens": 1280000,
  "balance": null
}
```

- Token 计数是非负 64 位整数，且完整数据必须满足 `total=input+output`。
- `completeness` 为 `complete`、`partial` 或 `unknown`；部分记录缺失时不得伪装完整。
- OpenAI 的 `coverage` 必须明确为 `organization` 或 `project`。
- DeepSeek 网关账本使用 `source=gateway_ledger`、`coverage=gateway_only`。
- DeepSeek 没有用量数据时三个 Token 字段均为 `null`，状态为 `not_configured`、
  `unsupported` 或 `unknown`，不得显示为 0。
- `balance` 有独立 `status`、`currency`、十进制定点字符串 `amount` 和 `fetched_at`；余额
  成功不能把 Token 状态变为成功。不同币种不相加。

## 本地日期窗口

`local_date` 的窗口是指定时区 `[00:00, now)`。例如北京时间 2026-10-18 00:00 对应
`2026-10-17T16:00:00Z`。缓存 key 至少包含设备、统计范围、时区、本地日期；跨日本地
日期后不能把前一天数字标成“今日”。服务端必须处理上游分页和桶边界，而不是只改日期标签。

## 错误与缓存

- 鉴权失败返回 401；限流返回 429；设备只做有上限的退避。
- 单一上游失败时仍返回 200 和其他模块；失败模块可携带可识别日期的最后有效值并标 `stale`。
- HTTP 200 只表示聚合响应成功，不代表所有源新鲜；设备依据各模块状态与时间显示。
- 服务端内部缓存可使用短 TTL，但必须在鉴权后读取并隔离设备与范围。
- 客户端缓存采用版本、长度和校验和验证，并以双槽/原子方式更新。

## 固件输入限制

- 响应硬上限 8 KiB；超限在解析前拒绝。
- 城市名最多 48 UTF-8 字节，天气文字最多 24 字节，重点文字最多 144 字节。
- 固件忽略 v1 的未知新增字段；拒绝未知主版本、负 Token、非整数 Token、整数溢出和
  `total != input + output` 的完整记录。
- 主页最多消费两个 usage 条目；更多条目视为协议错误，而非静默截断。

样例位于 `test/fixtures/dashboard/`。
