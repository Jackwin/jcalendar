#ifndef __DEEPSEEK_H__
#define __DEEPSEEK_H__

#include <Arduino.h>

enum DeepSeekDataStatus : int8_t {
    DEEPSEEK_NOT_STARTED = -1,
    DEEPSEEK_LOADING = 0,
    DEEPSEEK_OK = 1,
    DEEPSEEK_ERROR = 2,
    DEEPSEEK_NOT_CONFIGURED = 3,
    DEEPSEEK_UNSUPPORTED = 4,
    DEEPSEEK_UNKNOWN = 5,
};

// DeepSeek balance is independent from token usage. The public provider API
// currently used by this firmware exposes balance but no account-wide daily
// token endpoint.
struct DeepSeekData {
    DeepSeekDataStatus usageStatus = DEEPSEEK_UNSUPPORTED;
    bool hasUsage = false;
    uint64_t inputTokens = 0;
    uint64_t outputTokens = 0;
    uint64_t totalTokens = 0;
    String usageSource;
    String usageCoverage;

    DeepSeekDataStatus balanceStatus = DEEPSEEK_NOT_STARTED;
    bool isAvailable = false;
    String currency;       // CNY / USD
    String totalBalance;   // 总余额
    String grantedBalance; // 赠金余额
    String toppedUpBalance; // 充值余额
};

// 状态：-1 未开始, 0 进行中, 1 成功(至少一项), 2 失败, 3 未配置Key
int8_t deepseek_status();
DeepSeekData* deepseek_data();
void deepseek_exec(int status = 0);
void deepseek_exec_balance_only(); // 仅查余额，不调用 chat/completions
void deepseek_stop();

#endif
