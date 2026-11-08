#ifndef JCALENDAR_DASHBOARD_CLIENT_H
#define JCALENDAR_DASHBOARD_CLIENT_H

#include <Arduino.h>
#include <stddef.h>

#include "dashboard_model.h"

namespace dashboard {

enum class ClientResult : uint8_t {
    Ok = 0,
    NotConfigured,
    WifiUnavailable,
    TlsConfigurationError,
    Unauthorized,
    RateLimited,
    HttpError,
    ResponseTooLarge,
    Timeout,
    InvalidJson,
    UnsupportedSchema,
    InvalidData,
};

// Fetches and validates a snapshot. validatedJson is populated only on success
// and can be passed directly to dashboard_cache_store().
ClientResult dashboard_fetch(Snapshot& snapshot, String& validatedJson);

// Parses a bounded UTF-8 JSON payload. Exposed so the cache uses exactly the
// same validation path as a network response.
ClientResult dashboard_parse_json(const char* data, size_t length, Snapshot& snapshot);

const char* clientResultName(ClientResult result);

} // namespace dashboard

#endif
