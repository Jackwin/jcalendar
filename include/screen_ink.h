#ifndef __SCREEN_INK_H__
#define __SCREEN_INK_H__

#include <Arduino.h>

// 刷新模式：0=全刷（闪屏），1=LUT差分（无闪屏）
extern uint8_t _refresh_mode;

int si_calendar_status();
void si_calendar();

int si_wifi_status();
void si_wifi();

int si_weather_status();
void si_weather();

int si_screen_status();
void si_screen();
void si_screen_partial_status(); // 仅局部刷新状态栏（DeepSeek token + 电池），避免闪屏

void print_status();

void si_warning(const char* str);

#endif

void print_status();

void si_warning(const char* str);