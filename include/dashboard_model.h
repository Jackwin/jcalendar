#ifndef JCALENDAR_DASHBOARD_MODEL_H
#define JCALENDAR_DASHBOARD_MODEL_H

#include <Arduino.h>
#include <stdint.h>

namespace dashboard {

constexpr uint16_t SCHEMA_VERSION = 1;
constexpr size_t MAX_RESPONSE_BYTES = 8 * 1024;
constexpr size_t MAX_USAGE_PROVIDERS = 2;
constexpr size_t MAX_LOCATION_BYTES = 48;
constexpr size_t MAX_CONDITION_BYTES = 24;
constexpr size_t MAX_FOCUS_BYTES = 144;

enum class Status : uint8_t {
    Unknown = 0,
    Ok,
    Stale,
    NotConfigured,
    Unsupported,
    Error,
};

enum class Completeness : uint8_t {
    Unknown = 0,
    Complete,
    Partial,
};

struct OptionalInt32 {
    bool hasValue = false;
    int32_t value = 0;
};

struct OptionalUInt64 {
    bool hasValue = false;
    uint64_t value = 0;
};

struct WeatherData {
    Status status = Status::Unknown;
    String source;
    String observedAt;
    String fetchedAt;
    String locationName;
    OptionalInt32 tempC;
    OptionalInt32 tempMinC;
    OptionalInt32 tempMaxC;
    String condition;
    String conditionCode;
};

struct BalanceData {
    Status status = Status::Unknown;
    String currency;
    String amount;
    String fetchedAt;
};

struct UsageData {
    String provider;
    Status status = Status::Unknown;
    String source;
    String coverage;
    Completeness completeness = Completeness::Unknown;
    String windowStart;
    String windowEnd;
    String fetchedAt;
    String observedAt;
    OptionalUInt64 inputTokens;
    OptionalUInt64 outputTokens;
    OptionalUInt64 totalTokens;
    BalanceData balance;
};

struct FocusData {
    String type;
    String text;
};

struct Snapshot {
    uint16_t schemaVersion = 0;
    String generatedAt;
    String timezone;
    String localDate;
    WeatherData weather;
    UsageData usage[MAX_USAGE_PROVIDERS];
    uint8_t usageCount = 0;
    FocusData focus;
    uint32_t refreshAfterSeconds = 0;
};

const char* statusName(Status status);
bool parseStatus(const char* value, Status& status);
const char* completenessName(Completeness completeness);
bool parseCompleteness(const char* value, Completeness& completeness);
bool hasValidTokenTotal(const UsageData& usage);
bool isSupportedSchema(uint16_t schemaVersion);

} // namespace dashboard

#endif
