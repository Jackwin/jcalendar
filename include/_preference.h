#ifndef ___PREFERENCE_H__
#define ___PREFERENCE_H__

#include <Preferences.h>
#define PREF_NAMESPACE "J_CALENDAR"

// Preferences KEY定义
// !!!preferences key限制15字符
#define PREF_SI_CAL_DATE "SI_CAL_DATE" // 屏幕当前显示的日期
#define PREF_SI_WEEK_1ST "SI_WEEK_1ST" // 每周第一天，0: 周日（默认），1:周一
#define PREF_SI_TYPE "SI_TYPE" // 屏幕显示类型

#define PREF_QWEATHER_HOST "QWEATHER_HOST" // QWEATHER HOST
#define PREF_QWEATHER_KEY "QWEATHER_KEY" // QWEATHER KEY/TOKEN
#define PREF_QWEATHER_TYPE "QWEATHER_TYPE" // 0: 每日天气，1: 实时天气
#define PREF_QWEATHER_LOC "QWEATHER_LOC" // 地理位置
#define PREF_CD_DAY_DATE "CD_DAY_DATE" // 倒计日
#define PREF_CD_DAY_LABLE "CD_DAY_LABLE" // 倒计日名称
#define PREF_TAG_DAYS "TAG_DAYS" // tag day
#define PREF_STUDY_SCHEDULE "STUDY_SCH" // 课程表

// 假期信息，tm年，假期日(int8)，假期日(int8)...
#define PREF_HOLIDAY "HOLIDAY"

// DeepSeek（显示 token 用量 / 账户余额）
#define PREF_DS_KEY "DS_KEY"         // DeepSeek API Key
#define PREF_DS_TODAY_FLASH "DS_TOK_FLASH" // flash 今日累计 token
#define PREF_DS_TODAY_PRO "DS_TOK_PRO"   // pro 今日累计 token
#define PREF_DS_DATE "DS_TOK_DATE"   // 今日累计 token 对应的日期 YYYYMMDD

// 屏幕刷新模式：0=全刷（默认，闪屏），1=LUT差分刷新（无闪屏，快速）
#define PREF_REFRESH_MODE "REFRESH_MODE"

// 局部刷新计数（每 N 次局部刷新后做一次全刷，防残影）
#define PREF_PARTIAL_CNT "PARTIAL_CNT"
// 今日是否已完成首次全屏刷新 YYYYMMDD
#define PREF_FULL_DATE "FULL_DATE"

#endif
