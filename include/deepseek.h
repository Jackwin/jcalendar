#ifndef __DEEPSEEK_H__
#define __DEEPSEEK_H__

#include <Arduino.h>

// DeepSeek 数据
struct DeepSeekData {
    // 本次 deepseek-v4-flash 调用的 token 用量
    uint32_t flashPromptTokens;
    uint32_t flashCompletionTokens;
    uint32_t flashTotalTokens;
    // 本次 deepseek-v4-pro 调用的 token 用量
    uint32_t proPromptTokens;
    uint32_t proCompletionTokens;
    uint32_t proTotalTokens;
    String content; // 生成的文本（未在屏幕展示，预留）
    // 账户余额（来自 GET /user/balance）
    bool isAvailable;
    String currency;       // CNY / USD
    String totalBalance;   // 总余额
    String grantedBalance; // 赠金余额
    String toppedUpBalance; // 充值余额
    // 今日累计 token（跨日重置，NVS 持久化）
    uint32_t tokensTodayFlash;  // flash 今日累计
    uint32_t tokensTodayPro;    // pro 今日累计
};

// 状态：-1 未开始, 0 进行中, 1 成功(至少一项), 2 失败, 3 未配置Key
int8_t deepseek_status();
DeepSeekData* deepseek_data();
void deepseek_exec(int status = 0);
void deepseek_exec_balance_only(); // 仅查余额，不消耗 token（每小时调用）
void deepseek_stop();

#endif
