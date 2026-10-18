import test from "node:test";
import assert from "node:assert/strict";
import { readFile, readdir } from "node:fs/promises";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

import {
  authenticateDevice,
  buildDashboard,
  internalCacheTtl,
  localWindow,
  validateDashboard,
} from "../src/dashboard.js";
import { fetchOpenAIUsage } from "../src/providers.js";
import worker from "../openai_usage_proxy.js";

const device = {
  id: "desk-1", timezone: "Asia/Shanghai", locationName: "北京",
  qweatherLocation: "101010100", openaiProjectIds: ["proj_a"], focusText: "阅读20分钟",
};

test("Asia/Shanghai local day starts at previous UTC date", () => {
  const window = localWindow(new Date("2026-10-18T01:00:00Z"), "Asia/Shanghai");
  assert.equal(window.localDate, "2026-10-18");
  assert.equal(window.start.toISOString(), "2026-10-17T16:00:00.000Z");
  assert.equal(window.endOfLocalDay.toISOString(), "2026-10-18T16:00:00.000Z");
});

test("cache TTL expires at the local-day boundary", () => {
  const now = new Date("2026-10-18T15:59:50Z");
  const window = localWindow(now, "Asia/Shanghai");
  assert.equal(window.localDate, "2026-10-18");
  assert.equal(internalCacheTtl(now, window.endOfLocalDay, 300), 10);
  assert.equal(localWindow(new Date("2026-10-18T16:00:00Z"), "Asia/Shanghai").localDate, "2026-10-19");
});

test("authentication selects scope from server config", () => {
  const env = { DEVICE_CONFIG_JSON: JSON.stringify({ devices: [
    { id: "a", token: "token-a", timezone: "Asia/Shanghai", openaiProjectIds: ["proj_a"] },
    { id: "b", token: "token-b", timezone: "UTC", openaiProjectIds: ["proj_b"] },
  ] }) };
  const request = new Request("https://example.com/v1/dashboard?project_id=attacker", {
    headers: { Authorization: "Bearer token-b" },
  });
  const selected = authenticateDevice(request, env);
  assert.equal(selected.id, "b");
  assert.deepEqual(selected.openaiProjectIds, ["proj_b"]);
  assert.equal(authenticateDevice(new Request(request.url), env), null);
});

test("dashboard route enforces authentication and rate limiting", async () => {
  const unauthorized = await worker.fetch(
    new Request("https://example.com/v1/dashboard"),
    { DEVICE_TOKEN: "secret" },
    { waitUntil() {} },
  );
  assert.equal(unauthorized.status, 401);

  const limited = await worker.fetch(
    new Request("https://example.com/v1/dashboard", { headers: { Authorization: "Bearer secret" } }),
    { DEVICE_TOKEN: "secret", DEVICE_RATE_LIMITER: { limit: async () => ({ success: false }) } },
    { waitUntil() {} },
  );
  assert.equal(limited.status, 429);
  assert.equal(limited.headers.get("Retry-After"), "60");
  assert.equal(limited.headers.get("Cache-Control"), "private, no-store");
});

test("OpenAI usage follows every page and keeps the configured project filter", async (t) => {
  const calls = [];
  globalThis.fetch = async (url) => {
    calls.push(String(url));
    const second = new URL(url).searchParams.has("page");
    return new Response(JSON.stringify(second
      ? { data: [{ results: [{ input_tokens: 5, output_tokens: 7, input_cached_tokens: 1000 }] }], has_more: false }
      : { data: [{ results: [{ input_tokens: 10, output_tokens: 3 }] }], has_more: true, next_page: "next" }));
  };
  t.after(() => { delete globalThis.fetch; });
  const usage = await fetchOpenAIUsage({ OPENAI_ADMIN_KEY: "secret" }, device, 1, 2);
  assert.deepEqual(usage, { inputTokens: 15, outputTokens: 10, totalTokens: 25 });
  assert.equal(calls.length, 2);
  assert.ok(calls.every((url) => url.includes("project_ids=proj_a")));
  assert.ok(calls[1].includes("page=next"));
});

test("single-source failures remain isolated and null is not rewritten as zero", async (t) => {
  globalThis.fetch = async (url) => {
    const value = String(url);
    if (value.includes("api.openai.com")) return new Response("limited", { status: 429 });
    if (value.includes("api.deepseek.com")) return Response.json({ balance_infos: [{ currency: "CNY", total_balance: "83.20" }] });
    return new Response("weather failed", { status: 500 });
  };
  t.after(() => { delete globalThis.fetch; });
  const now = new Date("2026-10-18T01:00:00Z");
  const result = await buildDashboard({ OPENAI_ADMIN_KEY: "a", DEEPSEEK_API_KEY: "d", QWEATHER_KEY: "q" }, device, now, localWindow(now, device.timezone));
  assert.equal(result.weather.status, "error");
  assert.equal(result.usage[0].status, "stale");
  assert.equal(result.usage[0].total_tokens, null);
  assert.equal(result.usage[1].status, "unsupported");
  assert.equal(result.usage[1].balance.status, "ok");
});

test("validation rejects mismatched totals", () => {
  assert.throws(() => validateDashboard({
    schema_version: 1,
    weather: { status: "ok" },
    usage: [{ status: "ok", input_tokens: 1, output_tokens: 2, total_tokens: 9 }],
  }), /invalid token total/);
});

test("all checked-in dashboard fixtures fit the device envelope", async () => {
  const here = dirname(fileURLToPath(import.meta.url));
  const fixtureDirectory = resolve(here, "../../test/fixtures/dashboard");
  const names = (await readdir(fixtureDirectory)).filter((name) => name.endsWith(".json"));
  assert.equal(names.length, 6);
  for (const name of names) {
    const body = await readFile(join(fixtureDirectory, name));
    assert.ok(body.length > 0 && body.length <= 8 * 1024, `${name} exceeds the device body limit`);
    const fixture = JSON.parse(body.toString("utf8"));
    validateDashboard(fixture);
    assert.match(fixture.local_date, /^\d{4}-\d{2}-\d{2}$/);
    assert.ok(fixture.usage.length <= 2, `${name} has too many usage providers`);
  }
});
