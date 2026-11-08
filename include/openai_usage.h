#ifndef __OPENAI_USAGE_H__
#define __OPENAI_USAGE_H__

#include <Arduino.h>

struct OpenAIUsageData {
    uint32_t inputTokens;
    uint32_t outputTokens;
    uint32_t totalTokens;
    String dateUtc;
};

// Status: -1 not started, 0 running, 1 success, 2 request failed, 3 not configured.
int8_t openai_usage_status();
OpenAIUsageData* openai_usage_data();
void openai_usage_exec();
void openai_usage_stop();

#endif
