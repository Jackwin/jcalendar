import {
  authenticateDevice,
  buildDashboard,
  cacheIdentity,
  internalCacheTtl,
  localWindow,
  readInternalCache,
  writeInternalCache,
} from "./src/dashboard.js";
import { fetchOpenAIUsage } from "./src/providers.js";

const jsonResponse = (value, init = {}) => {
  const headers = new Headers(init.headers);
  headers.set("Content-Type", "application/json; charset=utf-8");
  headers.set("Cache-Control", "private, no-store");
  return new Response(JSON.stringify(value), { ...init, headers });
};

const unauthorized = () => new Response("Unauthorized", {
  status: 401,
  headers: { "Cache-Control": "private, no-store" },
});

async function rateLimited(env, device) {
  if (!env.DEVICE_RATE_LIMITER?.limit) return false;
  const result = await env.DEVICE_RATE_LIMITER.limit({ key: device.id });
  return !result.success;
}

async function legacyUsage(request, env, device) {
  const now = new Date();
  const utcDate = now.toISOString().slice(0, 10);
  const start = Date.parse(`${utcDate}T00:00:00Z`) / 1000;
  try {
    const usage = await fetchOpenAIUsage(env, device, start, Math.floor(now.getTime() / 1000));
    return jsonResponse({
      date_utc: utcDate,
      input_tokens: usage.inputTokens,
      output_tokens: usage.outputTokens,
      total_tokens: usage.totalTokens,
    });
  } catch (error) {
    const status = error.status === 429 ? 429 : 502;
    return jsonResponse({ error: "usage_provider_error" }, {
      status,
      headers: status === 429 ? { "Retry-After": String(error.retryAfter || 60) } : {},
    });
  }
}

export default {
  async fetch(request, env, ctx) {
    const url = new URL(request.url);
    if (request.method !== "GET") return new Response("Not found", { status: 404 });
    if (url.pathname !== "/v1/dashboard" && url.pathname !== "/v1/usage/today") {
      return new Response("Not found", { status: 404 });
    }

    const device = authenticateDevice(request, env);
    if (!device) return unauthorized();
    if (await rateLimited(env, device)) {
      return jsonResponse({ error: "rate_limited" }, {
        status: 429,
        headers: { "Retry-After": "60" },
      });
    }

    if (url.pathname === "/v1/usage/today") return legacyUsage(request, env, device);

    // Scope is selected only from authenticated server-side device config.
    // Query parameters never alter provider credentials, project filters, or location.
    const now = new Date();
    const window = localWindow(now, device.timezone);
    const cacheKey = await cacheIdentity(url.origin, device, window.localDate);
    const cached = await readInternalCache(cacheKey);
    if (cached) return jsonResponse(cached);

    const dashboard = await buildDashboard(env, device, now, window);
    const encoded = JSON.stringify(dashboard);
    if (new TextEncoder().encode(encoded).byteLength > 8192) {
      return jsonResponse({ error: "snapshot_too_large" }, { status: 502 });
    }

    const ttl = internalCacheTtl(now, window.endOfLocalDay, 300);
    if (ttl > 0) ctx.waitUntil(writeInternalCache(cacheKey, dashboard, ttl));
    return jsonResponse(dashboard);
  },
};
