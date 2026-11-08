#include "dashboard_model.h"

#include <cstring>

namespace dashboard {

const char* statusName(Status status) {
    switch (status) {
    case Status::Ok: return "ok";
    case Status::Stale: return "stale";
    case Status::NotConfigured: return "not_configured";
    case Status::Unsupported: return "unsupported";
    case Status::Error: return "error";
    case Status::Unknown: return "unknown";
    }
    return "unknown";
}

bool parseStatus(const char* value, Status& status) {
    if (value == nullptr) return false;
    if (strcmp(value, "ok") == 0) status = Status::Ok;
    else if (strcmp(value, "stale") == 0) status = Status::Stale;
    else if (strcmp(value, "not_configured") == 0) status = Status::NotConfigured;
    else if (strcmp(value, "unsupported") == 0) status = Status::Unsupported;
    else if (strcmp(value, "error") == 0) status = Status::Error;
    else if (strcmp(value, "unknown") == 0) status = Status::Unknown;
    else return false;
    return true;
}

const char* completenessName(Completeness completeness) {
    switch (completeness) {
    case Completeness::Complete: return "complete";
    case Completeness::Partial: return "partial";
    case Completeness::Unknown: return "unknown";
    }
    return "unknown";
}

bool parseCompleteness(const char* value, Completeness& completeness) {
    if (value == nullptr) return false;
    if (strcmp(value, "complete") == 0) completeness = Completeness::Complete;
    else if (strcmp(value, "partial") == 0) completeness = Completeness::Partial;
    else if (strcmp(value, "unknown") == 0) completeness = Completeness::Unknown;
    else return false;
    return true;
}

bool hasValidTokenTotal(const UsageData& usage) {
    if (!usage.inputTokens.hasValue || !usage.outputTokens.hasValue || !usage.totalTokens.hasValue) {
        return false;
    }
    if (UINT64_MAX - usage.inputTokens.value < usage.outputTokens.value) return false;
    return usage.totalTokens.value == usage.inputTokens.value + usage.outputTokens.value;
}

bool isSupportedSchema(uint16_t schemaVersion) {
    return schemaVersion == SCHEMA_VERSION;
}

} // namespace dashboard
