#ifndef __DAILY_BRIEF_H__
#define __DAILY_BRIEF_H__

#include <Arduino.h>

struct DailyBrief {
    String todo;
    String word;
    String meaning;
};

// Selects one short to-do item and one English word deterministically for a day.
// NVS values (TODO_LIST and WORD_LIST) override the built-in defaults when present.
DailyBrief daily_brief_for_day(int dayOfYear);

#endif
