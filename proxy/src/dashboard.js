import { fetchDeepSeekBalance, fetchOpenAIUsage, fetchWeather } from "./providers.js";

const STATUS = new Set(["ok", "stale", "not_configured", "unsupported", "error", "unknown"]);

function splitCsv(value) {
  return String(value || "").split(",").map((item) => item.trim()).filter(Boolean);
}

function configuredDevices(env) {
  if (!env.DEVICE_CONFIG_JSON) {
    return env.DEVICE_TOKEN ? [{
      id: env.DEVICE_ID || "default",
      token: env.DEVICE_TOKEN,
      timezone: env.TIMEZONE || "Asia/Shanghai",
      locationName: env.LOCATION_NAME || "",
      qweatherLocation: env.QWEATHER_LOCATION || "",
      openaiProjectIds: splitCsv(env.OPENAI_PROJECT_IDS),
      focusText: env.FOCUS_TEXT || "",
    }] : [];
  }
  const parsed = JSON.parse(env.DEVICE_CONFIG_JSON);
  if (!Array.isArray(parsed.devices)) throw new Error("DEVICE_CONFIG_JSON.devices must be an array");
  return parsed.devices;
}

export function authenticateDevice(request, env) {
  const auth = request.headers.get("Authorization") || "";
  if (!auth.startsWith("Bearer ")) return null;
  const token = auth.slice(7);
  const match = configuredDevices(env).find((item) => item.token === token);
  if (!match) return null;
  return {
    id: String(match.id || "default"),
    token,
    timezone: String(match.timezone || "Asia/Shanghai"),
    locationName: String(match.locationName || ""),
    qweatherLocation: String(match.qweatherLocation || ""),
    openaiProjectIds: Array.isArray(match.openaiProjectIds) ? match.openaiProjectIds.map(String) : [],
    focusText: String(match.focusText || ""),
  };
}

function partsAt(date, timezone) {
  const parts = new Intl.DateTimeFormat("en-CA", {
    timeZone: timezone,
    year: "numeric", month: "2-digit", day: "2-digit",
    hour: "2-digit", minute: "2-digit", second: "2-digit",
    hourCycle: "h23",
  }).formatToParts(date);
  return Object.fromEntries(parts.filter((part) => part.type !== "literal").map((part) => [part.type, Number(part.value)]));
}

function zonedTimeToUtc(parts, timezone) {
  const wanted = Date.UTC(parts.year, parts.month - 1, parts.day, parts.hour || 0, parts.minute || 0, parts.second || 0);
  let candidate = wanted;
  for (let i = 0; i < 3; i += 1) {
    const actual = partsAt(new Date(candidate), timezone);
    const represented = Date.UTC(actual.year, actual.month - 1, actual.day, actual.hour, actual.minute, actual.second);
    candidate += wanted - represented;
  }
  return new Date(candidate);
}

function isoDate(parts) {
  return `${String(parts.year).padStart(4, "0")}-${String(parts.month).padStart(2, "0")}-${String(parts.day).padStart(2, "0")}`;
}

function nextDate(parts) {
  const value = new Date(Date.UTC(parts.year, parts.month - 1, parts.day + 1));
  return { year: value.getUTCFullYear(), month: value.getUTCMonth() + 1, day: value.getUTCDate() };
}

export function localWindow(now, timezone) {
  const local = partsAt(now, timezone);
  const start = zonedTimeToUtc({ ...local, hour: 0, minute: 0, second: 0 }, timezone);
  const following = nextDate(local);
  const endOfLocalDay = zonedTimeToUtc({ ...following, hour: 0, minute: 0, second: 0 }, timezone);
  return { localDate: isoDate(local), start, end: now, endOfLocalDay };
}

export function internalCacheTtl(now, endOfLocalDay, maximumSeconds) {
  const toMidnight = Math.floor((endOfLocalDay.getTime() - now.getTime()) / 1000);
  return Math.max(0, Math.min(maximumSeconds, toMidnight));
}

export async function cacheIdentity(origin, device, localDate) {
  const scope = JSON.stringify({
    device: device.id,
    token: device.token,
    date: localDate,
    timezone: device.timezone,
    projects: device.openaiProjectIds,
    weather: device.qweatherLocation,
  });
  const hash = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(scope));
  const hex = [...new Uint8Array(hash)].map((byte) => byte.toString(16).padStart(2, "0")).join("");
  return new Request(`${origin}/internal/dashboard/${hex}`);
}

export async function readInternalCache(key) {
  if (typeof caches === "undefined" || !caches.default) return null;
  const response = await caches.default.match(key);
  return response ? response.json() : null;
}

export async function writeInternalCache(key, value, ttl) {
  if (typeof caches === "undefined" || !caches.default) return;
  const response = new Response(JSON.stringify(value), {
    headers: { "Content-Type": "application/json", "Cache-Control": `max-age=${ttl}` },
  });
  await caches.default.put(key, response);
}

const emptyWeather = (status, source, fetchedAt = null) => ({
  status, source, observed_at: null, fetched_at: fetchedAt, location_name: "",
  temp_c: null, temp_min_c: null, temp_max_c: null, condition: "", condition_code: "",
});

const usageBase = (provider, status, source, coverage, window, fetchedAt = null) => ({
  provider, status, source, coverage, completeness: "unknown",
  window_start: window.start.toISOString(), window_end: window.end.toISOString(),
  fetched_at: fetchedAt, observed_at: null,
  input_tokens: null, output_tokens: null, total_tokens: null, balance: null,
});

function providerError(error) {
  return error?.status === 429 ? "stale" : "error";
}

export async function buildDashboard(env, device, now, window) {
  const fetchedAt = now.toISOString();
  const [weatherResult, openaiResult, balanceResult] = await Promise.allSettled([
    fetchWeather(env, device, now),
    fetchOpenAIUsage(env, device, Math.floor(window.start.getTime() / 1000), Math.floor(now.getTime() / 1000)),
    fetchDeepSeekBalance(env),
  ]);

  let weather;
  if (!env.QWEATHER_KEY || !device.qweatherLocation) {
    weather = emptyWeather("not_configured", "qweather");
  } else if (weatherResult.status === "fulfilled") {
    weather = weatherResult.value;
  } else {
    weather = emptyWeather(providerError(weatherResult.reason), "qweather", fetchedAt);
    weather.location_name = device.locationName;
  }

  const openai = usageBase(
    "openai",
    env.OPENAI_ADMIN_KEY ? "unknown" : "not_configured",
    "provider_api",
    device.openaiProjectIds.length ? "project" : "organization",
    window,
  );
  if (env.OPENAI_ADMIN_KEY && openaiResult.status === "fulfilled") {
    Object.assign(openai, {
      status: "ok", completeness: "complete", fetched_at: fetchedAt,
      input_tokens: openaiResult.value.inputTokens,
      output_tokens: openaiResult.value.outputTokens,
      total_tokens: openaiResult.value.totalTokens,
    });
  } else if (env.OPENAI_ADMIN_KEY) {
    openai.status = providerError(openaiResult.reason);
    openai.fetched_at = fetchedAt;
  }

  const deepseek = usageBase("deepseek", "unsupported", "gateway_ledger", "gateway_only", window);
  deepseek.balance = { status: "not_configured", currency: "", amount: "", fetched_at: null };
  if (env.DEEPSEEK_API_KEY && balanceResult.status === "fulfilled") {
    deepseek.balance = { ...balanceResult.value, status: "ok", fetched_at: fetchedAt };
  } else if (env.DEEPSEEK_API_KEY) {
    deepseek.balance = { status: providerError(balanceResult.reason), currency: "", amount: "", fetched_at: fetchedAt };
  }

  const dashboard = {
    schema_version: 1,
    generated_at: fetchedAt,
    timezone: device.timezone,
    local_date: window.localDate,
    weather,
    usage: [openai, deepseek],
    focus: { type: "manual", text: device.focusText.slice(0, 48) },
    refresh_after_seconds: 7200,
  };
  validateDashboard(dashboard);
  return dashboard;
}

export function validateDashboard(value) {
  if (value.schema_version !== 1) throw new Error("unsupported schema");
  if (!STATUS.has(value.weather.status)) throw new Error("invalid weather status");
  for (const usage of value.usage) {
    if (!STATUS.has(usage.status)) throw new Error("invalid usage status");
    const values = [usage.input_tokens, usage.output_tokens, usage.total_tokens];
    if (values.every((item) => item !== null)) {
      if (!values.every(Number.isSafeInteger) || values.some((item) => item < 0)) throw new Error("invalid token count");
      if (usage.input_tokens + usage.output_tokens !== usage.total_tokens) throw new Error("invalid token total");
    } else if (!values.every((item) => item === null)) {
      usage.completeness = "partial";
    }
  }
}
