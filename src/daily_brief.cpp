#include "daily_brief.h"

#include <Preferences.h>

#include "_preference.h"

namespace {

const char* const DEFAULT_TODOS[] = {
    "整理桌面", "阅读20分钟", "喝一杯水", "复盘今天",
    "散步10分钟", "完成最重要的一件事", "联系一位朋友"
};

const char* const DEFAULT_WORDS[] = {
    "focus=专注", "learn=学习", "calm=平静", "create=创造",
    "kind=善意", "steady=坚持", "progress=进步"
};

String pickListItem(const String& list, const char* const defaults[], size_t defaultCount, int dayOfYear) {
    if (list.isEmpty()) return String(defaults[dayOfYear % defaultCount]);

    int count = 1;
    for (size_t i = 0; i < list.length(); ++i) {
        if (list.charAt(i) == '|') ++count;
    }

    const int target = dayOfYear % count;
    int current = 0;
    size_t start = 0;
    for (size_t i = 0; i <= list.length(); ++i) {
        if (i == list.length() || list.charAt(i) == '|') {
            if (current == target) return list.substring(start, i);
            start = i + 1;
            ++current;
        }
    }
    return String(defaults[dayOfYear % defaultCount]);
}

}  // namespace

DailyBrief daily_brief_for_day(int dayOfYear) {
    Preferences pref;
    pref.begin(PREF_NAMESPACE, true);
    const String todoList = pref.getString(PREF_TODO_LIST, "");
    const String wordList = pref.getString(PREF_WORD_LIST, "");
    pref.end();

    DailyBrief brief;
    brief.todo = pickListItem(todoList, DEFAULT_TODOS,
                              sizeof(DEFAULT_TODOS) / sizeof(DEFAULT_TODOS[0]), dayOfYear);
    const String wordItem = pickListItem(wordList, DEFAULT_WORDS,
                                         sizeof(DEFAULT_WORDS) / sizeof(DEFAULT_WORDS[0]), dayOfYear);
    const int separator = wordItem.indexOf('=');
    if (separator < 0) {
        brief.word = wordItem;
    } else {
        brief.word = wordItem.substring(0, separator);
        brief.meaning = wordItem.substring(separator + 1);
    }
    return brief;
}
