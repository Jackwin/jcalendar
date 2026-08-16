// Copy this file to include/openai_usage_secrets.h and fill in your proxy values.
// The real file is ignored by Git and never sent through the device web page.
#define OPENAI_USAGE_PROXY_URL "https://usage.example.com/v1/usage/today"
#define OPENAI_USAGE_PROXY_TOKEN "replace-with-a-random-32-byte-token"

// Optional: avoids placing the DeepSeek API key in the device HTTP page or NVS.
#define DEEPSEEK_API_KEY ""

// Public root CA for the proxy's HTTPS certificate, not the leaf certificate.
// Use a raw string literal to preserve PEM line breaks.
#define OPENAI_USAGE_PROXY_CA R"PEM(
-----BEGIN CERTIFICATE-----
replace-with-your-proxy-root-ca
-----END CERTIFICATE-----
)PEM"
