# OpenAI usage proxy

This Worker keeps the OpenAI organization admin key off the ESP32. Deploy it with a private Worker route or a custom HTTPS domain, then create encrypted secrets:

```sh
wrangler secret put OPENAI_ADMIN_KEY
wrangler secret put DEVICE_TOKEN
```

`DEVICE_TOKEN` should be a random value of at least 32 bytes. Configure a Cloudflare WAF rate-limit rule for `GET /v1/usage/today` (for example, 12 requests per minute per IP). The Worker returns only current UTC-day input/output totals and cannot proxy arbitrary OpenAI requests.

Deploy from this directory with `npx wrangler login`, `npx wrangler secret put OPENAI_ADMIN_KEY`, `npx wrangler secret put DEVICE_TOKEN`, then `npx wrangler deploy`. The included `wrangler.jsonc` requires both secrets before deployment. Copy `include/openai_usage_secrets.example.h` to `include/openai_usage_secrets.h`. Fill in the Worker URL ending in `/v1/usage/today`, `DEVICE_TOKEN`, and the PEM root CA that signs the Worker's HTTPS certificate. The PEM certificate is public; it enables the ESP32 to authenticate the proxy instead of accepting any certificate.

The real secrets header is Git-ignored and is never accepted by the device's HTTP configuration page or stored in NVS. For physical-device protection, enable ESP32 flash encryption and secure boot in production. If the device token is extracted, rotate only `DEVICE_TOKEN`; the OpenAI administrator key remains unaffected.
