// Cloudflare Worker: deploy this separately from the ESP32 firmware.
// Required encrypted secrets:
//   OPENAI_ADMIN_KEY - an OpenAI organization admin API key
//   DEVICE_TOKEN     - a random 32-byte or longer value, unique to this device
// The worker deliberately exposes one read-only aggregate endpoint only.
export default {
  async fetch(request, env, ctx) {
    const unauthorized = () => new Response("Unauthorized", { status: 401 });
    if (request.method !== "GET" || new URL(request.url).pathname !== "/v1/usage/today") {
      return new Response("Not found", { status: 404 });
    }
    const auth = request.headers.get("Authorization");
    if (!auth || auth !== `Bearer ${env.DEVICE_TOKEN}`) return unauthorized();

    const cacheKey = new Request(new URL(request.url).origin + "/internal/usage-cache");
    const cached = await caches.default.match(cacheKey);
    if (cached) return cached;

    const now = new Date();
    const start = Math.floor(Date.UTC(now.getUTCFullYear(), now.getUTCMonth(), now.getUTCDate()) / 1000);
    const end = Math.floor(Date.now() / 1000);
    const upstream = await fetch(
      `https://api.openai.com/v1/organization/usage/completions?start_time=${start}&end_time=${end}&bucket_width=1d&limit=1`,
      { headers: { Authorization: `Bearer ${env.OPENAI_ADMIN_KEY}` } }
    );
    if (!upstream.ok) return new Response("Usage provider error", { status: 502 });

    const data = await upstream.json();
    let input = 0, output = 0;
    for (const bucket of data.data ?? []) {
      for (const result of bucket.results ?? []) {
        input += result.input_tokens ?? 0;
        output += result.output_tokens ?? 0;
      }
    }
    const response = new Response(JSON.stringify({
      date_utc: now.toISOString().slice(0, 10),
      input_tokens: input,
      output_tokens: output,
      total_tokens: input + output
    }), {
      headers: {
        "Content-Type": "application/json",
        "Cache-Control": "public, max-age=300"
      }
    });
    ctx.waitUntil(caches.default.put(cacheKey, response.clone()));
    return response;
  }
};
