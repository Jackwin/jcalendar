#include "dashboard_renderer.h"

#include <cstring>

namespace dashboard {
namespace {

static_assert(8 + 384 <= 400 && 8 + 32 <= 300, "date rectangle exceeds screen");
static_assert(8 + 384 <= 400 && 48 + 70 <= 300, "weather rectangle exceeds screen");
static_assert(8 + 188 <= 400 && 126 + 100 <= 300, "OpenAI rectangle exceeds screen");
static_assert(204 + 188 <= 400 && 126 + 100 <= 300, "DeepSeek rectangle exceeds screen");
static_assert(8 + 384 <= 400 && 234 + 36 <= 300, "focus rectangle exceeds screen");
static_assert(8 + 384 <= 400 && 278 + 14 <= 300, "status rectangle exceeds screen");

bool rectFits(const Rect& rect) {
    return rect.x >= 0 && rect.y >= 0 && rect.width > 0 && rect.height > 0
        && rect.x + rect.width <= 400 && rect.y + rect.height <= 300;
}

String unsignedText(uint64_t value) {
    char buffer[24];
    snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
    return String(buffer);
}

String scaledTokenCount(uint64_t value, uint64_t divisor, char suffix, uint8_t level) {
    uint64_t tenths = (value / divisor) * 10;
    const uint64_t remainder = value % divisor;
    tenths += (remainder * 10 + divisor / 2) / divisor;
    if (tenths >= 10000 && level < 2) {
        return scaledTokenCount(value, divisor * 1000, level == 0 ? 'M' : 'B', level + 1);
    }
    String output = unsignedText(tenths / 10);
    if (tenths < 1000 && tenths % 10 != 0) {
        output += ".";
        output += static_cast<char>('0' + tenths % 10);
    }
    output += suffix;
    return output;
}

} // namespace

HomeLayout homeLayout() {
    return {
        {8, 8, 384, 32},
        {8, 48, 384, 70},
        {8, 126, 188, 100},
        {204, 126, 188, 100},
        {8, 234, 384, 36},
        {8, 278, 384, 14},
    };
}

bool layoutFits400x300(const HomeLayout& layout) {
    return rectFits(layout.date) && rectFits(layout.weather)
        && rectFits(layout.openai) && rectFits(layout.deepseek)
        && rectFits(layout.focus) && rectFits(layout.status);
}

const UsageData* findUsage(const Snapshot& snapshot, const char* provider) {
    for (uint8_t i = 0; i < snapshot.usageCount; ++i) {
        if (snapshot.usage[i].provider == provider) return &snapshot.usage[i];
    }
    return nullptr;
}

String formatTokenCount(uint64_t value) {
    if (value < 1000) return unsignedText(value);
    if (value < 1000000) return scaledTokenCount(value, 1000, 'K', 0);
    if (value < 1000000000) return scaledTokenCount(value, 1000000, 'M', 1);
    return scaledTokenCount(value, 1000000000, 'B', 2);
}

String usageValue(const UsageData* usage) {
    if (usage == nullptr || usage->status == Status::NotConfigured || usage->status == Status::Unsupported) {
        return "未接入";
    }
    if (!usage->totalTokens.hasValue) return "未知";
    return formatTokenCount(usage->totalTokens.value);
}

String usageScopeLabel(const UsageData* usage) {
    if (usage == nullptr) return "未配置";
    if (usage->coverage == "project") return "API项目·今日";
    if (usage->coverage == "organization") return "API组织·今日";
    if (usage->coverage == "gateway_only") return "网关统计·今日";
    return "范围未知";
}

String snapshotStateLabel(const Snapshot& snapshot, bool offline) {
    if (offline) return "离线 · 保留缓存";
    if (snapshot.weather.status == Status::Stale) return "天气数据有延迟";
    if (snapshot.weather.status == Status::Error) return "天气获取失败";
    for (uint8_t i = 0; i < snapshot.usageCount; ++i) {
        if (snapshot.usage[i].status == Status::Stale) return "用量数据有延迟";
        if (snapshot.usage[i].status == Status::Error) return "部分数据失败";
    }
    return "数据已更新";
}

} // namespace dashboard
