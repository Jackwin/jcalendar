function providerError(message, response) {
  const error = new Error(message);
  error.status = response?.status || 502;
  error.retryAfter = Number(response?.headers?.get("Retry-After")) || 60;
  return error;
}

function safeCount(value) {
  if (!Number.isSafeInteger(value) || value < 0) throw new Error("unsafe provider token count");
  return BigInt(value);
}

function toProtocolCount(value) {
  if (value > BigInt(Number.MAX_SAFE_INTEGER)) throw new Error("aggregate exceeds JSON safe integer range");
  return Number(value);
}

export async function fetchOpenAIUsage(env, device, start, end) {
  if (!env.OPENAI_ADMIN_KEY) throw providerError("OpenAI is not configured");
  let page = null;
  let input = 0n;
  let output = 0n;
  let pageCount = 0;
  do {
    const url = new URL("https://api.openai.com/v1/organization/usage/completions");
    url.searchParams.set("start_time", String(start));
    url.searchParams.set("end_time", String(end));
    url.searchParams.set("bucket_width", "1h");
    url.searchParams.set("limit", "168");
    for (const projectId of device.openaiProjectIds) url.searchParams.append("project_ids", projectId);
    if (page) url.searchParams.set("page", page);
    const response = await fetch(url, { headers: { Authorization: `Bearer ${env.OPENAI_ADMIN_KEY}` } });
    if (!response.ok) throw providerError("OpenAI usage request failed", response);
    const payload = await response.json();
    for (const bucket of payload.data || []) {
      for (const result of bucket.results || []) {
        input += safeCount(result.input_tokens || 0);
        output += safeCount(result.output_tokens || 0);
      }
    }
    page = payload.has_more ? payload.next_page : null;
    pageCount += 1;
    if (pageCount > 32) throw new Error("OpenAI pagination limit exceeded");
  } while (page);
  return {
    inputTokens: toProtocolCount(input),
    outputTokens: toProtocolCount(output),
    totalTokens: toProtocolCount(input + output),
  };
}

export async function fetchWeather(env, device, now) {
  if (!env.QWEATHER_KEY || !device.qweatherLocation) throw providerError("Weather is not configured");
  const host = env.QWEATHER_HOST || "api.qweather.com";
  const query = `location=${encodeURIComponent(device.qweatherLocation)}&key=${encodeURIComponent(env.QWEATHER_KEY)}`;
  const nowUrl = `https://${host}/v7/weather/now?${query}`;
  const dailyUrl = `https://${host}/v7/weather/3d?${query}`;
  const [currentResponse, dailyResponse] = await Promise.all([fetch(nowUrl), fetch(dailyUrl)]);
  if (!currentResponse.ok || !dailyResponse.ok) {
    throw providerError("Weather request failed", !currentResponse.ok ? currentResponse : dailyResponse);
  }
  const [current, daily] = await Promise.all([currentResponse.json(), dailyResponse.json()]);
  if (current.code !== "200" || daily.code !== "200" || !current.now || !daily.daily?.[0]) {
    throw providerError("Weather payload invalid");
  }
  const observedAt = new Date(current.now.obsTime);
  const ageSeconds = Math.max(0, (now.getTime() - observedAt.getTime()) / 1000);
  const status = ageSeconds > 2 * 3600 ? "stale" : "ok";
  const today = daily.daily[0];
  return {
    status,
    source: "qweather",
    observed_at: Number.isNaN(observedAt.getTime()) ? null : observedAt.toISOString(),
    fetched_at: now.toISOString(),
    location_name: device.locationName,
    temp_c: ageSeconds > 6 * 3600 ? null : Number(current.now.temp),
    temp_min_c: Number(today.tempMin),
    temp_max_c: Number(today.tempMax),
    condition: String(current.now.text || today.textDay || ""),
    condition_code: String(current.now.icon || today.iconDay || ""),
  };
}

export async function fetchDeepSeekBalance(env) {
  if (!env.DEEPSEEK_API_KEY) throw providerError("DeepSeek is not configured");
  const response = await fetch("https://api.deepseek.com/user/balance", {
    headers: { Authorization: `Bearer ${env.DEEPSEEK_API_KEY}` },
  });
  if (!response.ok) throw providerError("DeepSeek balance request failed", response);
  const payload = await response.json();
  const balance = payload.balance_infos?.[0];
  if (!balance || typeof balance.total_balance !== "string") throw providerError("DeepSeek balance payload invalid");
  return { currency: String(balance.currency || ""), amount: balance.total_balance };
}
