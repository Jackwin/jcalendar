# J-Calendar dashboard Worker

Cloudflare Worker 聚合 OpenAI API 用量、和风天气和 DeepSeek 余额。供应商密钥只存在于
Worker secret，ESP32 仅持有设备读取令牌。

## 路由

- `GET /v1/dashboard`：v1 聚合快照，按设备配置使用本地日窗口。
- `GET /v1/usage/today`：兼容旧固件的 UTC 日 OpenAI 用量响应。

两个路由都要求 `Authorization: Bearer <device token>`。客户端响应总是
`Cache-Control: private, no-store`。内部 Cache API 快照最多缓存 300 秒，并在设备本地
日期切换前到期；cache key 包含设备令牌摘要、范围、时区、位置和本地日期。

## 配置

单设备部署至少设置：

```sh
wrangler secret put DEVICE_TOKEN
wrangler secret put OPENAI_ADMIN_KEY
wrangler secret put QWEATHER_KEY
wrangler secret put DEEPSEEK_API_KEY
```

非敏感变量可在 Worker 配置中设置：

```text
DEVICE_ID=desk-1
TIMEZONE=Asia/Shanghai
LOCATION_NAME=北京
QWEATHER_LOCATION=101010100
QWEATHER_HOST=api.qweather.com
OPENAI_PROJECT_IDS=proj_abc,proj_def
FOCUS_TEXT=阅读20分钟
```

未设置 `OPENAI_PROJECT_IDS` 时范围明确为整个组织；设置后只查询服务端列出的项目。
请求 query string 不能覆盖项目、位置或供应商 URL。

多设备可使用 secret `DEVICE_CONFIG_JSON`：

```json
{
  "devices": [
    {
      "id": "desk-1",
      "token": "a-long-random-device-token",
      "timezone": "Asia/Shanghai",
      "locationName": "北京",
      "qweatherLocation": "101010100",
      "openaiProjectIds": ["proj_abc"],
      "focusText": "阅读20分钟"
    }
  ]
}
```

生产环境应绑定 Cloudflare Rate Limiting binding `DEVICE_RATE_LIMITER`；超限返回 429 和有限
的 `Retry-After`。也可在 WAF 对两个 GET 路由增加按 IP/设备的限流。

DeepSeek Token 首版固定返回 `unsupported` 和 `null`，余额有独立状态。只有接入真实业务
网关账本后才允许返回 `gateway_only` 用量；Worker 不会为查询用量调用模型。

## OpenAI 聚合

Worker 使用组织 usage/completions 接口的 `[start_time,end_time)`，按本地日计算 UTC 边界，
采用一小时 bucket 并跟随所有 `next_page`。总量只等于 `input_tokens + output_tokens`；缓存
输入是输入细分，不会重复相加。计数使用 BigInt 聚合，并拒绝非安全整数、负数和异常分页。

## 验证与部署

```sh
cd proxy
npm test
npx wrangler deploy
```

测试覆盖北京时间跨日、跨日缓存 TTL、设备鉴权与范围隔离、OpenAI 分页、429/单源失败、
余额与 Token 独立状态，以及总量校验。部署前仍需用真实的最小权限凭据做 staging 联调。
