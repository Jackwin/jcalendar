#include "screen_ink.h"


#include <weather.h>
#include <API.hpp>
#include <deepseek.h>
#include <daily_brief.h>
#include "holiday.h"
#include "nongli.h"

#include "battery.h"
int voltage;

// 刷新模式：0=全刷（闪屏），1=LUT差分（无闪屏）
uint8_t _refresh_mode = 0;

#include <_preference.h>

#include <U8g2_for_Adafruit_GFX.h>
#include <GxEPD2_3C.h>
#include "GxEPD2_display_selection_new_style.h"

#include "font.h"
#define ROTATION 0
#define CALENDAR_PANEL_HEIGHT 190

#include "wiring.h"
GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS, GxEPD2_DRIVER_CLASS::HEIGHT> display(GxEPD2_DRIVER_CLASS(/*CS=D8*/ SPI_CS, /*DC=D3*/ SPI_DC, /*RST=D4*/ SPI_RST, /*BUSY=D2*/ SPI_BUSY));
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts;

#define FONT_TEXT u8g2_font_wqy16_t_gb2312 // 224825bytes，最大字库（天气描述中“霾”，只有此字库中有）
#define FONT_SUB u8g2_font_wqy12_t_gb2312 // 次要字体，u8g2最小字体
const String week_str[] = { "日", "一", "二", "三", "四", "五", "六" };
// const String tg_str[] = { "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸" };            // 天干
// const String dz_str[] = { "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥" }; // 地支
// const String sx_str[] = { "鼠", "牛", "虎", "兔", "龙", "蛇", "马", "羊", "猴", "鸡", "狗", "猪" }; // 生肖
const String nl10_str[] = { "初", "十", "廿", "卅" }; // 农历十位
const String nl_str[] = { "十", "一", "二", "三", "四", "五", "六", "七", "八", "九", "十" }; // 农历个位
const String nl_mon_str[] = { "", "正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "冬", "腊" }; // 农历首位

int _screen_status = -1;
int _calendar_status = -1;
String _cd_day_label;
String _cd_day_date;
String _tag_days_str;
int _week_1st;
int lunarDates[31];
int jqAccDate[24]; // 节气积累日

const int jrDate[] = { 101, 214, 308, 312, 501, 504, 601, 701, 801, 910, 1001, 1224, 1225 };
const String jrText[] = { "元旦", "情人节", "妇女节", "植树节", "劳动节", "青年节", "儿童节", "建党节", "建军节", "教师节", "国庆节", "平安夜", "圣诞节" };
const size_t jrLength = sizeof(jrDate) / sizeof(jrDate[0]);

int _si_type = 0;
String _study_schedule;


struct tm tmInfo = { 0 }; // 日历显示用的时间

struct
{
    int16_t topX;
    int16_t topY;
    int16_t topW;
    int16_t topH;

    int16_t tX;
    int16_t tY;
    int16_t tW;
    int16_t tH;

    int16_t yearX;
    int16_t yearY;
    int16_t weekX;
    int16_t weekY;

    int16_t lunarYearX;
    int16_t lunarYearY;
    int16_t lunarDayX;
    int16_t lunarDayY;
    int16_t cdDayX;
    int16_t cdDayY;

    int16_t statusX;
    int16_t statusY;
    int16_t statusW;
    int16_t statusH;

    int16_t weatherX;
    int16_t weatherY;
    int16_t weatherW;
    int16_t weatherH;

    int16_t headerX;
    int16_t headerY;
    int16_t headerW;
    int16_t headerH;

    int16_t daysX;
    int16_t daysY;
    int16_t daysW;
    int16_t daysH;
    int16_t dayW;
    int16_t dayH;
} static calLayout;

TaskHandle_t SCREEN_HANDLER;

void init_cal_layout_size() {
    // ===== 坐标系定义（400x300 三色屏） =====
    // Y:  0- 30  顶部日期行（2026年7月19日）
    // Y: 30- 40  第几周
    // Y: 40- 50  倒计日（可选）
    // Y: 50- 64  星期头部（日一二三四五六）
    // Y: 64-232  日历网格（6行 × 28px/行 = 168px）
    // Y:234-238  分隔线（粗黑线）
    // Y:238-298  下半区：DeepSeek Token + 三天天气

    calLayout.topX = 0;      calLayout.topY = 0;
    calLayout.topW = 160;    calLayout.topH = 30;

    calLayout.yearX = 5;     calLayout.yearY = 26;
    calLayout.weekX = 5;     calLayout.weekY = 38;

    calLayout.lunarYearX = 170;  calLayout.lunarYearY = 12;
    calLayout.lunarDayX = 170;   calLayout.lunarDayY = 26;

    calLayout.statusX = 300;     calLayout.statusY = 0;
    calLayout.statusW = 100;     calLayout.statusH = 16;
    calLayout.weatherX = 300;    calLayout.weatherY = 0;
    calLayout.weatherW = 100;    calLayout.weatherH = 60;

    calLayout.cdDayX = 0;    calLayout.cdDayY = 48;

    calLayout.headerX = 0;   calLayout.headerY = 50;
    calLayout.headerW = 400; calLayout.headerH = 14;

    calLayout.daysX = 0;     calLayout.daysY = 64;
    calLayout.daysW = 400;   calLayout.daysH = 168;  // 6 × 28
    calLayout.dayW = 57;     calLayout.dayH = 28;
}

void draw_cal_header() {
    uint16_t color;

    u8g2Fonts.setFont(FONT_TEXT);
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFontDirection(0);
    u8g2Fonts.setForegroundColor(GxEPD_WHITE);
    int16_t daysMagin = 4;

    for (int i = 0; i < 7; i++) {
        if ((i + _week_1st) % 7 == 0 || (i + _week_1st) % 7 == 6) {
            color = GxEPD_RED;
        } else {
            color = GxEPD_BLACK;
        }
        // header background
        if (i == 0) {
            display.fillRect(0, calLayout.headerY, (display.width() - 7 * calLayout.dayW) / 2, calLayout.headerH, color);
        } else if (i == 6) {
            display.fillRect((display.width() + 7 * calLayout.dayW) / 2, calLayout.headerY, (display.width() - 7 * calLayout.dayW) / 2, calLayout.headerH, color);
        }
        display.fillRect((display.width() - 7 * calLayout.dayW) / 2 + i * calLayout.dayW, calLayout.headerY, calLayout.dayW, calLayout.headerH, color);

        // header text
        u8g2Fonts.drawUTF8(calLayout.headerX + daysMagin + (calLayout.dayW - u8g2Fonts.getUTF8Width(week_str[i].c_str())) / 2 + i * calLayout.dayW, calLayout.headerY + calLayout.headerH - 3, week_str[(i + _week_1st) % 7].c_str());
    }
}

uint16_t todayColor = GxEPD_BLACK;
String todayLunarYear;
String todayLunarDay;

// 更新年份
void draw_cal_year(bool partial) {
    if (partial) {
        display.setPartialWindow(calLayout.topX, calLayout.topY, calLayout.topW + calLayout.tW, calLayout.topH);
        display.firstPage();
        display.fillScreen(GxEPD_WHITE);
    }

    // 日期
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFontDirection(0);
    u8g2Fonts.setForegroundColor(todayColor);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setCursor(calLayout.yearX, calLayout.yearY);
    u8g2Fonts.setFont(u8g2_font_fub25_tn);
    u8g2Fonts.print(String(tmInfo.tm_year + 1900).c_str());
    u8g2Fonts.setFont(FONT_TEXT);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.print("年");
    u8g2Fonts.setFont(u8g2_font_fub25_tn);
    u8g2Fonts.setForegroundColor(todayColor);
    u8g2Fonts.print(String(tmInfo.tm_mon + 1).c_str());
    u8g2Fonts.setFont(FONT_TEXT);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.print("月");
    u8g2Fonts.setFont(u8g2_font_fub25_tn);
    u8g2Fonts.setForegroundColor(todayColor);
    u8g2Fonts.printf(String(tmInfo.tm_mday).c_str());
    u8g2Fonts.setFont(FONT_TEXT);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.print("日");

    calLayout.lunarYearX = u8g2Fonts.getCursorX() + 10;
    calLayout.lunarDayX = u8g2Fonts.getCursorX() + 10;

    // 第几周
    u8g2Fonts.setFont(FONT_TEXT);
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFontDirection(0);
    u8g2Fonts.setForegroundColor(todayColor);
    u8g2Fonts.setCursor(calLayout.weekX, calLayout.weekY);
    char week_num[8];
    strftime(week_num, sizeof(week_num), "%V", &tmInfo); // 国际标准（ISO 8601）‌：以周一作为每周起始日
    u8g2Fonts.printf("第%s周", week_num);
    calLayout.cdDayX = u8g2Fonts.getCursorX(); // update cd day X;

    // 今日农历年份，e.g. 乙巳年 蛇
    // 如果农历月份小于公历月份，那么说明是上一年
    u8g2Fonts.setCursor(calLayout.lunarYearX, calLayout.lunarYearY);
    u8g2Fonts.setFont(FONT_SUB);
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFontDirection(0);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.print(todayLunarYear);

    // 今日农历日期
    u8g2Fonts.setCursor(calLayout.lunarDayX, calLayout.lunarDayY);
    u8g2Fonts.setFont(FONT_SUB);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    if (!todayLunarDay.isEmpty()) {
        u8g2Fonts.print(todayLunarDay);
    } else {
        u8g2Fonts.setFont(FONT_TEXT);
        u8g2Fonts.print(("星期" + week_str[tmInfo.tm_wday]).c_str());
    }

    // 特殊日期
    // draw_special_day();

    if (partial) {
        display.nextPage();
    }
}

void draw_cal_days(bool partial) {
    if (partial) {
        display.setPartialWindow(calLayout.daysX, calLayout.daysY, calLayout.daysW, calLayout.daysH);
        display.firstPage();
        display.fillScreen(GxEPD_WHITE);
    }

    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFontDirection(0);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);

    size_t totalDays = 30; // 小月
    int monthNum = tmInfo.tm_mon + 1;
    if (monthNum == 1 || monthNum == 3 || monthNum == 5 || monthNum == 7 || monthNum == 8 || monthNum == 10 || monthNum == 12) { // 大月
        totalDays = 31;
    }
    if (monthNum == 2) {
        if ((tmInfo.tm_year + 1900) % 4 == 0
            && (tmInfo.tm_year + 1900) % 100 != 0
            || (tmInfo.tm_year + 1900) % 400 == 0) {
            totalDays = 29; // 闰二月
        } else {
            totalDays = 28; // 二月
        }
    }

    // 计算本月第一天星期几
    int wday1 = (36 - tmInfo.tm_mday + tmInfo.tm_wday) % 7;
    // 计算本月第一天是全年的第几天（0～365）
    int yday1 = tmInfo.tm_yday - tmInfo.tm_mday + 1;

    // 确认哪些日期需要打tag
    char tags[31] = { 0 };
    int indexBegin = 0;
    while (_tag_days_str.length() >= (indexBegin + 9)) {
        String y = _tag_days_str.substring(indexBegin, indexBegin + 4);
        String m = _tag_days_str.substring(indexBegin + 4, indexBegin + 6);
        String d = _tag_days_str.substring(indexBegin + 6, indexBegin + 8);
        char t = _tag_days_str.charAt(indexBegin + 8);

        const int tagDay = d.toInt();
        if ((y.equals(String(tmInfo.tm_year + 1900)) || y.equals("0000"))
            && (m.equals(String(tmInfo.tm_mon + 1)) || m.equals("00"))
            && tagDay >= 1 && tagDay <= 31) {
            tags[tagDay - 1] = t;
        }

        // Serial.printf("Format: %s, %s, %s, %c\n", y.c_str(), m.c_str(), d.c_str(), t);

        indexBegin = indexBegin + 9;
        while (indexBegin < _tag_days_str.length() && (_tag_days_str.charAt(indexBegin) < '0' || _tag_days_str.charAt(indexBegin) > '9')) { // 搜索字符串直到下个字符是0-9之间的
            indexBegin++;
        }
    }

    Holiday _holiday;
    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    size_t holiday_size = pref.getBytesLength(PREF_HOLIDAY);
    if (holiday_size > 0) {
        pref.getBytes(PREF_HOLIDAY, &_holiday, holiday_size);
    }
    pref.end();

    if (_holiday.year != tmInfo.tm_year + 1900 || _holiday.month != tmInfo.tm_mon + 1) {
        _holiday = {};
    }

    int jqIndex = 0;
    int jrIndex = 0;
    int shiftDay = (wday1 - _week_1st) >= 0 ? 0 : 7;
    int16_t dh = calLayout.dayH;
    int16_t dw = calLayout.dayW;
    for (size_t iDay = 0; iDay < totalDays; iDay++) {
        uint8_t num = wday1 + iDay - _week_1st + shiftDay;
        uint8_t column = num % 7;
        uint8_t row = num / 7;
        int16_t x = calLayout.daysX + column * dw;
        int16_t y = calLayout.daysY + row * dh;

        // 周六、日，字体红色
        uint16_t color;
        if ((wday1 + iDay) % 7 == 0 || (wday1 + iDay) % 7 == 6) {
            color = GxEPD_RED;
        } else {
            color = GxEPD_BLACK;
        }

        if (tmInfo.tm_year + 1900 == _holiday.year && tmInfo.tm_mon + 1 == _holiday.month) {
            uint8_t holidayIndex = 0;
            for (; holidayIndex < _holiday.length; holidayIndex++) {
                if (abs(_holiday.holidays[holidayIndex]) == (iDay + 1)) {
                    // 显示公休、调班logo和颜色
                    u8g2Fonts.setFont(u8g2_font_open_iconic_all_1x_t);
                    if (_holiday.holidays[holidayIndex] > 0) { // 公休
                        color = GxEPD_RED;
                        u8g2Fonts.setForegroundColor(color);
                        u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
                        u8g2Fonts.drawUTF8(x + dw - 6, y + dh / 2, "\u006c");
                    } else if (_holiday.holidays[holidayIndex] < 0) { // 调班
                        color = GxEPD_BLACK;
                        u8g2Fonts.setForegroundColor(color);
                        u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
                        u8g2Fonts.drawUTF8(x + dw - 6, y + dh / 2, "\u0064");
                    }
                    break;
                }
            }
        }
        u8g2Fonts.setForegroundColor(color); // 设置整体颜色
        u8g2Fonts.setBackgroundColor(GxEPD_WHITE);

        // 画日历日期数字
        u8g2Fonts.setFont(u8g2_font_fub17_tn);
        int16_t numX = x + (dw - u8g2Fonts.getUTF8Width(String(iDay + 1).c_str())) / 2;
        int16_t numY = y + dh - 6;
        u8g2Fonts.drawUTF8(numX, numY, String(iDay + 1).c_str());

        // 画节气&节日&农历
        String lunarStr = "";
        int lunarDate = lunarDates[iDay];
        int isLeapMon = lunarDate < 0 ? 1 : 0; // 闰月
        lunarDate = abs(lunarDate);
        int lunarMon = lunarDate / 100;
        int lunarDay = lunarDate % 100;

        bool isJq = false; // 是否节气
        int accDays0 = tmInfo.tm_yday + 1 - tmInfo.tm_mday; // 本月0日的积累日（tm_yday 从0开始，tm_mday从1开始, i从0开始）
        for (; jqIndex < 24; jqIndex++) {
            if (accDays0 + iDay + 1 < jqAccDate[jqIndex]) {
                break;
            }
            if (accDays0 + iDay + 1 == jqAccDate[jqIndex]) {
                lunarStr = String(nl_jq_text[jqIndex]);
                isJq = true;
                break;
            }
        }
        bool isJr = false; // 是否节日
        int currentDateNum = (tmInfo.tm_mon + 1) * 100 + iDay + 1;
        for (; jrIndex < jrLength; jrIndex++) {
            if (currentDateNum < jrDate[jrIndex]) {
                break;
            }
            if (currentDateNum == jrDate[jrIndex]) {
                lunarStr = jrText[jrIndex];
                isJr = true;
                break;
            }
        }
        if (!isJq && !isJr) { // 农历
            if (lunarDay == 1) {
                // 初一，显示月份
                lunarStr = (isLeapMon == 0 ? "" : "闰") + nl_mon_str[lunarMon] + "月";
            } else {
                if (lunarDay == 10) {
                    lunarStr = "初十";
                } else if (lunarDay == 20) {
                    lunarStr = "二十";
                } else if (lunarDay == 30) {
                    lunarStr = "三十";
                } else {
                    // 其他日期
                    lunarStr = nl10_str[lunarDay / 10] + nl_str[lunarDay % 10];
                }
            }
            if (lunarMon == 1 && lunarDay == 1) {
                lunarStr = "春节";
            } else if (lunarMon == 1 && lunarDay == 15) {
                lunarStr = "元宵节";
            } else if (lunarMon == 5 && lunarDay == 5) {
                lunarStr = "端午节";
            } else if (lunarMon == 7 && lunarDay == 7) {
                lunarStr = "七夕节";
            } else if (lunarMon == 8 && lunarDay == 15) {
                lunarStr = "中秋节";
            } else if (lunarMon == 9 && lunarDay == 9) {
                lunarStr = "重阳节";
            }
        }
        // 画节气/节日/农历文字
        u8g2Fonts.setFont(FONT_SUB);
        u8g2Fonts.drawUTF8(x + (dw - u8g2Fonts.getUTF8Width(lunarStr.c_str())) / 2, y + dh - 2, lunarStr.c_str());

        // 今日日期
        if ((iDay + 1) == tmInfo.tm_mday) { // 双线加粗
            todayColor = color;

            // 加框线
            display.drawRoundRect(x, y + 1, dw, dh, 4, GxEPD_RED);
            display.drawRoundRect(x + 1, y + 2, dw - 2, dh - 2, 3, GxEPD_RED);

            // 今日农历年份，e.g. 乙巳年 蛇
            // 如果农历月份小于公历月份，那么说明是上一年
            int tg = nl_tg(tmInfo.tm_year + 1900 - (lunarMon > (tmInfo.tm_mon + 1) ? 1 : 0));
            int dz = nl_dz(tmInfo.tm_year + 1900 - (lunarMon > (tmInfo.tm_mon + 1) ? 1 : 0));;
            todayLunarYear = String(nl_tg_text[tg]) + String(nl_dz_text[dz]) + "年 " + String(nl_sx_text[dz]);

            // 今日农历日期
            if (lunarDay == 10) {
                lunarStr = "初十";
            } else if (lunarDay == 20) {
                lunarStr = "二十";
            } else if (lunarDay == 30) {
                lunarStr = "三十";
            } else {
                // 其他日期
                lunarStr = nl10_str[lunarDay / 10] + nl_str[lunarDay % 10];
            }
            todayLunarDay = (isLeapMon == 0 ? "" : "闰") + nl_mon_str[lunarMon] + "月" + lunarStr;
        }

        // 画日期Tag
        const char* tagChar = NULL;
        if (tags[iDay] == 'a') { //tag
            tagChar = "\u0042";
        } else if (tags[iDay] == 'b') { // dollar
            tagChar = "\u0024";
        } else if (tags[iDay] == 'c') { // smile
            tagChar = "\u0053";
        } else if (tags[iDay] == 'd') { // warning
            tagChar = "\u0021";
        }
        if (tagChar != NULL) {
            u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
            u8g2Fonts.setForegroundColor(GxEPD_RED);
            u8g2Fonts.setFont(u8g2_font_twelvedings_t_all);
            int iconX = numX - u8g2Fonts.getUTF8Width(tagChar) - 1;
            iconX = iconX <= (x + 3) ? (iconX + 1) : iconX;
            int iconY = y + dh / 2;
            u8g2Fonts.drawUTF8(iconX, iconY, tagChar);
        }

        // 画Calendar提示点
        /*
        Calendar* cal = weather_cal();
        for(int calIndex = 0; calIndex < cal->length; calIndex ++) {
            CalEvent event = cal->events[calIndex];
            if(event.dt_begin.substring(0, 4).toInt() == (tmInfo.tm_year + 1900)
            && event.dt_begin.substring(4, 6).toInt() == (tmInfo.tm_mon + 1)
            && event.dt_begin.substring(6, 8).toInt() == (iDay + 1)) {
                u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
                u8g2Fonts.setForegroundColor(GxEPD_RED);
                u8g2Fonts.setFont(u8g2_font_siji_t_6x10);
                u8g2Fonts.drawUTF8(x + 42, y + 12, "\ue015");
            }
        }
        */
    }
}

// draw countdown-day info
void draw_cd_day(String label, String date) {
    if (label == NULL || date == NULL || label.length() == 0 || date.length() != 8) {
        Serial.print("Invalid countdown-day parameters.\n");
        return;
    }

    long d = atol(date.c_str());

    struct tm today = { 0 }; // 今日0秒
    today.tm_year = tmInfo.tm_year;
    today.tm_mon = tmInfo.tm_mon;
    today.tm_mday = tmInfo.tm_mday;
    today.tm_hour = 0;
    today.tm_min = 0;
    today.tm_sec = 0;
    time_t todayT = mktime(&today);

    struct tm someday = { 0 }; // 倒计日0秒
    someday.tm_year = d / 10000 - 1900;
    someday.tm_mon = d % 10000 / 100 - 1;
    someday.tm_mday = d % 100;
    someday.tm_hour = 0;
    someday.tm_min = 0;
    someday.tm_sec = 0;
    time_t somedayT = mktime(&someday);

    /*
    char buffer[80];
    strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", &someday);
    Serial.printf("CD day: %s\n", buffer);
    */

    long diff = somedayT - todayT;
    if (diff < 0) return; // 倒计日已过

    int16_t beginX = calLayout.cdDayX;
    int16_t endX = calLayout.weatherX;
    int16_t y = calLayout.cdDayY;

    if (diff == 0) {
        String prefix = "今日 ";
        String suffix = " ！！！";
        u8g2Fonts.setFont(FONT_SUB);
        int16_t preWidth = u8g2Fonts.getUTF8Width(prefix.c_str());
        int16_t suffixWidth = u8g2Fonts.getUTF8Width(suffix.c_str());
        u8g2Fonts.setFont(FONT_SUB);
        int16_t labelWidth = u8g2Fonts.getUTF8Width(label.c_str());
        int16_t margin = (endX - beginX - preWidth - labelWidth - suffixWidth) / 2;

        u8g2Fonts.setCursor((margin > 0 ? margin : 0) + beginX, y); // 居中显示
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        u8g2Fonts.setFont(FONT_SUB);
        u8g2Fonts.print(prefix.c_str()); // 今天
        u8g2Fonts.setForegroundColor(GxEPD_RED);
        u8g2Fonts.setFont(FONT_TEXT);
        u8g2Fonts.print(label.c_str()); // ****
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        u8g2Fonts.setFont(FONT_SUB);
        u8g2Fonts.print(suffix.c_str()); // ！
    } else if (diff > 0) {
        String prefix = "距 ";
        String middle = " 还有 ";
        int iDiff = diff / (60 * 60 * 24);
        char days[11] = "";
        itoa(iDiff, days, 10);
        String suffix = " 天";

        u8g2Fonts.setFont(FONT_SUB);
        int16_t preWidth = u8g2Fonts.getUTF8Width(prefix.c_str());
        int16_t midWidth = u8g2Fonts.getUTF8Width(middle.c_str());
        int16_t suffixWidth = u8g2Fonts.getUTF8Width(suffix.c_str());
        u8g2Fonts.setFont(FONT_SUB);
        int16_t labelWidth = u8g2Fonts.getUTF8Width(label.c_str());
        u8g2Fonts.setFont(u8g2_font_fub14_tn);
        int16_t daysWidth = u8g2Fonts.getUTF8Width(days);
        int16_t margin = (endX - beginX - preWidth - labelWidth - midWidth - daysWidth - suffixWidth) / 2;

        u8g2Fonts.setCursor((margin > 0 ? margin : 0) + beginX, y); // 居中显示
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        u8g2Fonts.setFont(FONT_SUB);
        u8g2Fonts.print(prefix.c_str()); // 距
        u8g2Fonts.setFont(FONT_TEXT);
        u8g2Fonts.print(label.c_str()); // ****
        u8g2Fonts.setFont(FONT_SUB);
        u8g2Fonts.print(middle.c_str()); // 还有
        u8g2Fonts.setForegroundColor(GxEPD_RED);
        u8g2Fonts.setFont(u8g2_font_fub14_tn);
        u8g2Fonts.print(days); // 0000
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        u8g2Fonts.setFont(FONT_SUB);
        u8g2Fonts.print(suffix.c_str()); // 天
    }
}


void draw_special_day() {
    String str = "Special Days!!!";

    u8g2Fonts.setCursor(u8g2Fonts.getCursorX() + 12, u8g2Fonts.getCursorY());
    u8g2Fonts.setForegroundColor(GxEPD_RED);
    u8g2Fonts.setFont(u8g2_font_open_iconic_all_2x_t);
    u8g2Fonts.print("\u00b7"); // 爱心
    u8g2Fonts.setFont(FONT_TEXT);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.print(str.c_str());
    u8g2Fonts.setForegroundColor(GxEPD_RED);
    u8g2Fonts.setFont(u8g2_font_open_iconic_all_2x_t);
    u8g2Fonts.print("\u00b7"); // 爱心
}

bool isNight(String time) {
    uint8_t hour = time.substring(11, 13).toInt();
    return hour < 6 || hour >= 18;
}

const char* getWeatherIcon(uint16_t id, bool fill) {
    switch (id) {
    case 100: return !fill ? "\uf101" : "\uf1ac";
    case 101: return !fill ? "\uf102" : "\uf1ad";
    case 102: return !fill ? "\uf103" : "\uf1ae";
    case 103: return !fill ? "\uf104" : "\uf1af";
    case 104: return !fill ? "\uf105" : "\uf1b0";
    case 150: return !fill ? "\uf106" : "\uf1b1";
    case 151: return !fill ? "\uf107" : "\uf1b2";
    case 152: return !fill ? "\uf108" : "\uf1b3";
    case 153: return !fill ? "\uf109" : "\uf1b4";
    case 300: return !fill ? "\uf10a" : "\uf1b5";
    case 301: return !fill ? "\uf10b" : "\uf1b6";
    case 302: return !fill ? "\uf10c" : "\uf1b7";
    case 303: return !fill ? "\uf10d" : "\uf1b8";
    case 304: return !fill ? "\uf10e" : "\uf1b9";
    case 305: return !fill ? "\uf10f" : "\uf1ba";
    case 306: return !fill ? "\uf110" : "\uf1bb";
    case 307: return !fill ? "\uf111" : "\uf1bc";
    case 308: return !fill ? "\uf112" : "\uf1bd";
    case 309: return !fill ? "\uf113" : "\uf1be";
    case 310: return !fill ? "\uf114" : "\uf1bf";
    case 311: return !fill ? "\uf115" : "\uf1c0";
    case 312: return !fill ? "\uf116" : "\uf1c1";
    case 313: return !fill ? "\uf117" : "\uf1c2";
    case 314: return !fill ? "\uf118" : "\uf1c3";
    case 315: return !fill ? "\uf119" : "\uf1c4";
    case 316: return !fill ? "\uf11a" : "\uf1c5";
    case 317: return !fill ? "\uf11b" : "\uf1c6";
    case 318: return !fill ? "\uf11c" : "\uf1c7";
    case 350: return !fill ? "\uf11d" : "\uf1c8";
    case 351: return !fill ? "\uf11e" : "\uf1c9";
    case 399: return !fill ? "\uf11f" : "\uf1ca";
    case 400: return !fill ? "\uf120" : "\uf1cb";
    case 401: return !fill ? "\uf121" : "\uf1cc";
    case 402: return !fill ? "\uf122" : "\uf1cd";
    case 403: return !fill ? "\uf123" : "\uf1ce";
    case 404: return !fill ? "\uf124" : "\uf1cf";
    case 405: return !fill ? "\uf125" : "\uf1d0";
    case 406: return !fill ? "\uf126" : "\uf1d1";
    case 407: return !fill ? "\uf127" : "\uf1d2";
    case 408: return !fill ? "\uf128" : "\uf1d3";
    case 409: return !fill ? "\uf129" : "\uf1d4";
    case 410: return !fill ? "\uf12a" : "\uf1d5";
    case 456: return !fill ? "\uf12b" : "\uf1d6";
    case 457: return !fill ? "\uf12c" : "\uf1d7";
    case 499: return !fill ? "\uf12d" : "\uf1d8";
    case 500: return !fill ? "\uf12e" : "\uf1d9";
    case 501: return !fill ? "\uf12f" : "\uf1da";
    case 502: return !fill ? "\uf130" : "\uf1db";
    case 503: return !fill ? "\uf131" : "\uf1dc";
    case 504: return !fill ? "\uf132" : "\uf1dd";
    case 507: return !fill ? "\uf133" : "\uf1de";
    case 508: return !fill ? "\uf134" : "\uf1df";
    case 509: return !fill ? "\uf135" : "\uf1e0";
    case 510: return !fill ? "\uf136" : "\uf1e1";
    case 511: return !fill ? "\uf137" : "\uf1e2";
    case 512: return !fill ? "\uf138" : "\uf1e3";
    case 513: return !fill ? "\uf139" : "\uf1e4";
    case 514: return !fill ? "\uf13a" : "\uf1e5";
    case 515: return !fill ? "\uf13b" : "\uf1e6";
    case 800: return "\uf13c";
    case 801: return "\uf13d";
    case 802: return "\uf13e";
    case 803: return "\uf13f";
    case 804: return "\uf140";
    case 805: return "\uf141";
    case 806: return "\uf142";
    case 807: return "\uf143";
    case 900: return !fill ? "\uf144" : "\uf1e7";
    case 901: return !fill ? "\uf145" : "\uf1e8";
    case 999:
    default: return !fill ? "\uf146" : "\uf1e9";
    }
}

// 画天气信息
#include "API.hpp"
void draw_weather(bool partial) {
    if (partial) {
        display.setPartialWindow(calLayout.weatherX, calLayout.weatherY, calLayout.weatherW, calLayout.weatherH - 1); // 高度减1，防止干扰到其他区域颜色
        display.firstPage();
        display.fillScreen(GxEPD_WHITE);
    }

    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFontDirection(0);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    if (weather_type() == 1) {
        // 实时天气
        Weather* wNow = weather_data_now();
        /* 更新排版，将字体缩小，与每日天气风格一致
        // 天气图标
        u8g2Fonts.setFont(u8g2_font_qweather_icon_16);
        u8g2Fonts.setCursor(calLayout.weatherX + 2, calLayout.weatherY + 30);
        u8g2Fonts.print(getWeatherIcon(wNow->icon, isNight(wNow->time)));

        // 天气文字
        u8g2Fonts.setFont(FONT_TEXT);
        uint16_t w1 = u8g2Fonts.getUTF8Width(wNow->text.c_str());
        u8g2Fonts.setCursor(calLayout.weatherX + 30, calLayout.weatherY + 18);
        u8g2Fonts.print((wNow->text).c_str());

        // 温湿度
        u8g2Fonts.setFont(u8g2_font_tenthinnerguys_tf);
        u8g2Fonts.setCursor(calLayout.weatherX + 30, calLayout.weatherY + 35);
        u8g2Fonts.printf("%d°C | %d%%", wNow->temp, wNow->humidity);

        // 风向级别
        u8g2Fonts.setCursor(calLayout.weatherX, calLayout.weatherY + calLayout.weatherH - 6);
        u8g2Fonts.setFont(FONT_TEXT);
        u8g2Fonts.printf("%s", wNow->windDir.c_str());
        u8g2Fonts.setFont(u8g2_font_fub14_tn);
        u8g2Fonts.printf(" %d ", wNow->windScale);
        u8g2Fonts.setFont(FONT_TEXT);
        u8g2Fonts.printf("级");
        */
        // 天气图标
        u8g2Fonts.setFont(u8g2_font_qweather_icon_16);
        u8g2Fonts.setCursor(calLayout.weatherX, calLayout.weatherY + 44);
        u8g2Fonts.print(getWeatherIcon(wNow->icon, isNight(wNow->time)));

        // 天气文字
        u8g2Fonts.setFont(FONT_SUB);
        uint16_t w1 = u8g2Fonts.getUTF8Width(wNow->text.c_str());
        u8g2Fonts.setCursor(calLayout.weatherX + 28, calLayout.weatherY + 22);
        u8g2Fonts.print(wNow->text.c_str());

        u8g2Fonts.setFont(u8g2_font_tenthinnerguys_tf);
        u8g2Fonts.setCursor(calLayout.weatherX + 28, calLayout.weatherY + 37);
        u8g2Fonts.printf("%d°C | %d%%", wNow->temp, wNow->humidity);

        // 风向级别
        u8g2Fonts.setCursor(calLayout.weatherX + 28, calLayout.weatherY + calLayout.weatherH - 6);
        u8g2Fonts.setFont(FONT_SUB);
        u8g2Fonts.printf("%s", wNow->windDir.c_str());
        u8g2Fonts.setCursor(u8g2Fonts.getCursorX() + 5, u8g2Fonts.getCursorY());
        u8g2Fonts.setFont(u8g2_font_tenthinnerguys_tf);
        u8g2Fonts.printf("%d", wNow->windScale);
        u8g2Fonts.setCursor(u8g2Fonts.getCursorX() + 5, u8g2Fonts.getCursorY());
        u8g2Fonts.setFont(FONT_SUB);
        u8g2Fonts.printf("级");
    } else {
        // 每日天气
        DailyForecast* wFc = weather_data_daily();
        DailyWeather* dw = wFc->weather;
        if (wFc->length == 0) {
            return;
        }
        DailyWeather wToday = dw[0];

        // 天气图标
        u8g2Fonts.setFont(u8g2_font_qweather_icon_16);
        u8g2Fonts.setCursor(calLayout.weatherX, calLayout.weatherY + 44);
        u8g2Fonts.print(getWeatherIcon(wToday.iconDay, false));

        // 天气文字
        u8g2Fonts.setFont(FONT_SUB);
        uint16_t w1 = u8g2Fonts.getUTF8Width(wToday.textDay.c_str());
        u8g2Fonts.setCursor(calLayout.weatherX + 28, calLayout.weatherY + 22);
        u8g2Fonts.print((wToday.textDay).c_str());

        u8g2Fonts.setFont(u8g2_font_tenthinnerguys_tf);
        u8g2Fonts.setCursor(u8g2Fonts.getCursorX() + 5, calLayout.weatherY + 22);
        u8g2Fonts.printf("%d%%", wToday.humidity);

        // 温度
        u8g2Fonts.setFont(u8g2_font_tenthinnerguys_tf);
        u8g2Fonts.setCursor(calLayout.weatherX + 28, calLayout.weatherY + 37);
        u8g2Fonts.printf("%d - %d°C", wToday.tempMin, wToday.tempMax);

        // 风向级别
        u8g2Fonts.setCursor(calLayout.weatherX + 28, calLayout.weatherY + calLayout.weatherH - 6);
        u8g2Fonts.setFont(FONT_SUB);
        u8g2Fonts.printf("%s", wToday.windDirDay.c_str());
        u8g2Fonts.setFont(u8g2_font_tenthinnerguys_tf);
        u8g2Fonts.printf(" %d ", wToday.windScaleDay);
        u8g2Fonts.setFont(FONT_SUB);
        u8g2Fonts.printf("级");
    }

    if (partial) {
        display.nextPage();
    }
}

// Draw err
void draw_err(bool partial) {
    if (partial) {
        display.setPartialWindow(380, 0, 20, 20);
        display.firstPage();
        display.fillScreen(GxEPD_WHITE);
    }
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFontDirection(0);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setForegroundColor(GxEPD_RED);
    u8g2Fonts.setFont(u8g2_font_open_iconic_all_2x_t);
    u8g2Fonts.setCursor(382, 18);
    u8g2Fonts.print("\u0118");

    if (partial) {
        display.nextPage();
    }
}

void draw_status(bool partial) {
    if (partial) {
        display.setPartialWindow(calLayout.statusX, calLayout.statusY, calLayout.statusW, calLayout.statusH);
        display.firstPage();
        display.fillScreen(GxEPD_WHITE);
    }
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFontDirection(0);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);

    // DeepSeek 今日token用量 + 账户余额（电池图标左侧）
    Serial.printf("[draw_status] deepseek_status=%d\r\n", deepseek_status());
    if (deepseek_status() == 1) {
        DeepSeekData* ds = deepseek_data();
        String text = String(ds->tokensTodayFlash + ds->tokensTodayPro) + "t ";
        if (ds->totalBalance.length() > 0) {
            String curSym = (ds->currency == "USD") ? "$" : "¥";
            text += curSym + ds->totalBalance;
        }
        Serial.printf("[draw_status] text=%s\r\n", text.c_str());

        // 红色背景框确认位置
        u8g2Fonts.setFont(FONT_SUB);
        int16_t textW = u8g2Fonts.getUTF8Width(text.c_str());
        int16_t x = 400 - 12 - 4 - textW;
        int16_t y = 10;
        display.fillRect(x - 2, 0, textW + 4, 16, GxEPD_RED);

        Serial.printf("[draw_status] draw at x=%d y=%d w=%d\r\n", x, y, textW);
        u8g2Fonts.setForegroundColor(GxEPD_WHITE);
        u8g2Fonts.drawUTF8(x, y, text.c_str());
    } else {
        // 未查询到 token 也显示提示
        Serial.printf("[draw_status] no data, status=%d\r\n", deepseek_status());
    }

    u8g2Fonts.setFont(u8g2_font_siji_t_6x10);

    // 电池icon
    String iconStr = "";
    if(voltage >= 4100) { // 满电
        iconStr = "\ue24b";
    } else if (voltage >= 3900) { // 多电
        iconStr = "\ue249";
    } else if (voltage >= 3700) { // 中电量
        iconStr = "\ue247";
    } else if (voltage >= 3500) { // 低电量
        iconStr = "\ue245";
    } else { // 空
        iconStr = "\ue242";
    }
    u8g2Fonts.drawUTF8(400 - 12 - 4, 10, iconStr.c_str());

    if (partial) {
        display.nextPage();
    }
}

static String compactLunarLabel(int day) {
    int lunarDate = lunarDates[day - 1];
    const bool leapMonth = lunarDate < 0;
    lunarDate = abs(lunarDate);
    const int lunarMonth = lunarDate / 100;
    const int lunarDay = lunarDate % 100;

    // Solar terms and public festivals take precedence over the regular lunar day.
    const int accumulatedDays = tmInfo.tm_yday + 1 - tmInfo.tm_mday + day;
    for (int i = 0; i < 24; ++i) {
        if (jqAccDate[i] == accumulatedDays) return String(nl_jq_text[i]);
    }
    const int dateNumber = (tmInfo.tm_mon + 1) * 100 + day;
    for (size_t i = 0; i < jrLength; ++i) {
        if (jrDate[i] == dateNumber) return jrText[i];
    }

    if (lunarMonth == 1 && lunarDay == 1) return "春节";
    if (lunarMonth == 1 && lunarDay == 15) return "元宵";
    if (lunarMonth == 5 && lunarDay == 5) return "端午";
    if (lunarMonth == 7 && lunarDay == 7) return "七夕";
    if (lunarMonth == 8 && lunarDay == 15) return "中秋";
    if (lunarMonth == 9 && lunarDay == 9) return "重阳";

    if (lunarDay == 1) {
        return String(leapMonth ? "闰" : "") + nl_mon_str[lunarMonth] + "月";
    }
    if (lunarDay == 10) return "初十";
    if (lunarDay == 20) return "二十";
    if (lunarDay == 30) return "三十";
    return nl10_str[lunarDay / 10] + nl_str[lunarDay % 10];
}

static String clipUtf8ToWidth(String text, uint16_t maxWidth) {
    while (text.length() > 0 && u8g2Fonts.getUTF8Width(text.c_str()) > maxWidth) {
        size_t cut = text.length() - 1;
        while (cut > 0 && (static_cast<uint8_t>(text.charAt(cut)) & 0xC0) == 0x80) --cut;
        text.remove(cut);
    }
    return text;
}

static String compactWeatherText() {
    if (weather_status() != 1) return "天气--";
    if (weather_type() == 1) {
        Weather* now = weather_data_now();
        return now->text + " " + String(now->temp) + "°";
    }
    DailyForecast* forecast = weather_data_daily();
    if (forecast->length == 0) return "天气--";
    DailyWeather& today = forecast->weather[0];
    return today.textDay + " " + String(today.tempMin) + "-" + String(today.tempMax) + "°";
}

static void draw_daily_brief(int16_t baselineY) {
    const DailyBrief brief = daily_brief_for_day(tmInfo.tm_yday);
    u8g2Fonts.setFont(FONT_SUB);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);

    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    const String weatherText = clipUtf8ToWidth(compactWeatherText(), 82);
    u8g2Fonts.drawUTF8(3, baselineY, weatherText.c_str());

    u8g2Fonts.setForegroundColor(GxEPD_RED);
    const String todoText = clipUtf8ToWidth(String("• ") + brief.todo, 190);
    u8g2Fonts.drawUTF8(88, baselineY, todoText.c_str());

    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    String wordText = brief.word;
    if (!brief.meaning.isEmpty()) wordText += "·" + brief.meaning;
    wordText = clipUtf8ToWidth(wordText, 116);
    const int16_t wordX = display.width() - 3 - u8g2Fonts.getUTF8Width(wordText.c_str());
    u8g2Fonts.drawUTF8(wordX, baselineY, wordText.c_str());
}

// Compact calendar with Gregorian day and lunar/solar-term label in every cell.
void draw_compact_calendar() {
    const int16_t calendarHeight = CALENDAR_PANEL_HEIGHT;
    const int16_t monthY = 20;
    const int16_t headerY = 28;
    const int16_t daysY = 46;
    const int16_t dayW = display.width() / 7;
    const int16_t dayH = (calendarHeight - daysY) / 6;

    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFontDirection(0);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setFont(u8g2_font_fub17_tn);
    char month[16];
    snprintf(month, sizeof(month), "%04d-%02d", tmInfo.tm_year + 1900, tmInfo.tm_mon + 1);
    u8g2Fonts.drawUTF8((display.width() - u8g2Fonts.getUTF8Width(month)) / 2, monthY, month);

    u8g2Fonts.setFont(FONT_SUB);
    for (int column = 0; column < 7; ++column) {
        uint16_t color = ((column + _week_1st) % 7 == 0 || (column + _week_1st) % 7 == 6)
                             ? GxEPD_RED : GxEPD_BLACK;
        u8g2Fonts.setForegroundColor(color);
        const String& week = week_str[(column + _week_1st) % 7];
        u8g2Fonts.drawUTF8(column * dayW + (dayW - u8g2Fonts.getUTF8Width(week.c_str())) / 2,
                           headerY + 12, week.c_str());
    }
    display.drawFastHLine(0, headerY + 16, display.width(), GxEPD_BLACK);

    int firstWday = (36 - tmInfo.tm_mday + tmInfo.tm_wday) % 7;
    int shiftDay = (firstWday - _week_1st) >= 0 ? 0 : 7;
    int daysInMonth = 31;
    int monthNum = tmInfo.tm_mon + 1;
    if (monthNum == 4 || monthNum == 6 || monthNum == 9 || monthNum == 11) daysInMonth = 30;
    if (monthNum == 2) {
        int year = tmInfo.tm_year + 1900;
        daysInMonth = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 29 : 28;
    }

    for (int day = 1; day <= daysInMonth; ++day) {
        int index = firstWday + day - 1 - _week_1st + shiftDay;
        int column = index % 7;
        int row = index / 7;
        int16_t x = column * dayW;
        int16_t y = daysY + row * dayH;
        uint16_t color = ((firstWday + day - 1) % 7 == 0 || (firstWday + day - 1) % 7 == 6)
                             ? GxEPD_RED : GxEPD_BLACK;
        u8g2Fonts.setForegroundColor(color);
        u8g2Fonts.setFont(u8g2_font_fub11_tn);
        String dayText = String(day);
        u8g2Fonts.drawUTF8(x + (dayW - u8g2Fonts.getUTF8Width(dayText.c_str())) / 2,
                            y + 11, dayText.c_str());

        String lunarText = compactLunarLabel(day);
        u8g2Fonts.setFont(FONT_SUB);
        u8g2Fonts.drawUTF8(x + (dayW - u8g2Fonts.getUTF8Width(lunarText.c_str())) / 2,
                           y + dayH - 2, lunarText.c_str());
        if (day == tmInfo.tm_mday) {
            display.drawRoundRect(x + 1, y, dayW - 2, dayH, 3, GxEPD_RED);
        }
    }
}

static void draw_weather_forecast_card(int16_t x, int16_t y, int16_t width, int16_t height) {
    display.drawRoundRect(x, y, width, height, 4, GxEPD_BLACK);
    display.fillRoundRect(x, y, width, 16, 4, GxEPD_BLACK);

    u8g2Fonts.setFont(FONT_SUB);
    u8g2Fonts.setBackgroundColor(GxEPD_BLACK);
    u8g2Fonts.setForegroundColor(GxEPD_WHITE);
    const char* title = "天气预报";
    u8g2Fonts.drawUTF8(x + (width - u8g2Fonts.getUTF8Width(title)) / 2, y + 12, title);

    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    DailyForecast* forecast = weather_data_daily();
    if (weather_status() != 1 || forecast->length == 0) {
        u8g2Fonts.setForegroundColor(GxEPD_RED);
        const char* message = weather_status() == 3 ? "天气未配置" : "天气不可用";
        u8g2Fonts.drawUTF8(x + (width - u8g2Fonts.getUTF8Width(message)) / 2, y + 52, message);
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        return;
    }

    const uint8_t days = forecast->length < 3 ? forecast->length : 3;
    const int16_t columnW = width / days;
    const char* labels[] = { "今天", "明天", "后天" };
    for (uint8_t day = 0; day < days; ++day) {
        const DailyWeather& item = forecast->weather[day];
        const int16_t centerX = x + day * columnW + columnW / 2;
        u8g2Fonts.setFont(u8g2_font_6x10_tf);
        u8g2Fonts.drawUTF8(centerX - u8g2Fonts.getUTF8Width(labels[day]) / 2, y + 30, labels[day]);

        u8g2Fonts.setFont(FONT_SUB);
        const String text = clipUtf8ToWidth(item.textDay, columnW - 6);
        u8g2Fonts.drawUTF8(centerX - u8g2Fonts.getUTF8Width(text.c_str()) / 2, y + 45, text.c_str());

        char temperature[16];
        snprintf(temperature, sizeof(temperature), "%d/%d°", item.tempMax, item.tempMin);
        u8g2Fonts.setFont(u8g2_font_6x10_tf);
        u8g2Fonts.drawUTF8(centerX - u8g2Fonts.getUTF8Width(temperature) / 2, y + 60, temperature);

        char humidity[16];
        snprintf(humidity, sizeof(humidity), "湿%d%%", item.humidity);
        u8g2Fonts.drawUTF8(centerX - u8g2Fonts.getUTF8Width(humidity) / 2, y + 76, humidity);
        if (day + 1 < days) display.drawFastVLine(x + (day + 1) * columnW, y + 22, height - 27, GxEPD_BLACK);
    }
}

void draw_deepseek_token_panel() {
    const int16_t top = CALENDAR_PANEL_HEIGHT;
    const int16_t briefH = 16;
    const int16_t margin = 4;
    const int16_t gap = 4;
    const int16_t cardW = (display.width() - margin * 2 - gap) / 2;
    const int16_t cardH = display.height() - top - briefH - 5;
    const int16_t deepseekX = margin + cardW + gap;
    const int16_t cardY = top + briefH + 1;
    const int16_t headerH = 16;
    const int16_t barMargin = 8;
    const int16_t barW = cardW - barMargin * 2;
    char text[64];

    display.drawFastHLine(0, top, display.width(), GxEPD_BLACK);
    draw_daily_brief(top + 12);
    display.drawFastHLine(0, top + briefH, display.width(), GxEPD_BLACK);
    draw_weather_forecast_card(margin, cardY, cardW, cardH);
    display.drawRoundRect(deepseekX, cardY, cardW, cardH, 4, GxEPD_BLACK);
    display.fillRoundRect(deepseekX, cardY, cardW, headerH, 4, GxEPD_BLACK);
    u8g2Fonts.setFontMode(1);
    u8g2Fonts.setFontDirection(0);
    u8g2Fonts.setFont(FONT_SUB);

    u8g2Fonts.setBackgroundColor(GxEPD_BLACK);
    u8g2Fonts.setForegroundColor(GxEPD_WHITE);
    const char* deepseekTitle = "DeepSeek · Today";
    u8g2Fonts.drawUTF8(deepseekX + (cardW - u8g2Fonts.getUTF8Width(deepseekTitle)) / 2,
                       cardY + 12, deepseekTitle);

    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    if (deepseek_status() == 1) {
        DeepSeekData* ds = deepseek_data();
        uint32_t total = ds->tokensTodayFlash + ds->tokensTodayPro;
        u8g2Fonts.setFont(u8g2_font_fub17_tn);
        snprintf(text, sizeof(text), "%u", total);
        u8g2Fonts.drawUTF8(deepseekX + (cardW - u8g2Fonts.getUTF8Width(text)) / 2,
                           cardY + 39, text);
        u8g2Fonts.setFont(u8g2_font_6x10_tf);
        snprintf(text, sizeof(text), "FLASH %u    PRO %u", ds->tokensTodayFlash, ds->tokensTodayPro);
        u8g2Fonts.drawUTF8(deepseekX + (cardW - u8g2Fonts.getUTF8Width(text)) / 2,
                           cardY + 54, text);
        if (total > 0) {
            int16_t flashW = (int16_t)((uint32_t)barW * ds->tokensTodayFlash / total);
            int16_t proW = barW - flashW;
            if (flashW > 0) display.fillRect(deepseekX + barMargin, cardY + 59, flashW, 6, GxEPD_RED);
            if (proW > 0) display.fillRect(deepseekX + barMargin + flashW, cardY + 59, proW, 6, GxEPD_BLACK);
        } else {
            display.drawRect(deepseekX + barMargin, cardY + 59, barW, 6, GxEPD_BLACK);
        }
        u8g2Fonts.drawUTF8(deepseekX + (cardW - u8g2Fonts.getUTF8Width("RED FLASH / BLACK PRO")) / 2,
                           cardY + 80, "RED FLASH / BLACK PRO");
    } else {
        u8g2Fonts.setForegroundColor(GxEPD_RED);
        u8g2Fonts.setFont(FONT_SUB);
        const char* message = deepseek_status() == 3 ? "Key not configured" : "Usage unavailable";
        u8g2Fonts.drawUTF8(deepseekX + (cardW - u8g2Fonts.getUTF8Width(message)) / 2,
                           cardY + 51, message);
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    }
}

void drawStudySchedule() {
    int i = _study_schedule.substring(0, 3).toInt();
    Serial.printf("%d\r\n", i);
    int morningClassCount = i / 100;
    int eveningClassCount = (i % 100) / 10;
    int nightClassCount = i % 10;
    int segmentCount = (morningClassCount > 0 ? 1 : 0) + (eveningClassCount > 0 ? 1 : 0) + (nightClassCount > 0 ? 1 : 0);
    String daysClassStr[7];
    int pos_begin = 4;
    int daysCount = 0;
    do {
        int pos_end = _study_schedule.indexOf(";", pos_begin);
        if (pos_end < 0) break;

        String str = _study_schedule.substring(pos_begin, pos_end);
        str.trim();
        daysClassStr[daysCount++] = str;
        // Serial.printf("%d : %d : %s\r\n", daysCount, pos_end, str.c_str());
        pos_begin = pos_end + 1;
    } while (true);

    // 计算Cell尺寸
    int cellHeight = (display.height() - calLayout.topH - calLayout.headerH - segmentCount * 2) / (morningClassCount + eveningClassCount + nightClassCount);
    int cellWidth = display.width() / daysCount;
    int marginLeft = (display.width() - cellWidth * daysCount) / 2;

    // cell line
    display.drawFastHLine(0, calLayout.topH, display.width(), GxEPD_RED);
    display.drawFastHLine(0, calLayout.topH - 1, display.width(), GxEPD_RED);
    display.drawFastHLine(0, calLayout.topH + calLayout.headerH - 1, display.width(), GxEPD_RED);
    display.drawFastHLine(0, calLayout.topH + calLayout.headerH, display.width(), GxEPD_RED);
    int segmentIndex = 0;
    uint16_t max_y = calLayout.topH + calLayout.headerH; // 水平分割线
    for (int x = 1; x <= morningClassCount; x++) {
        max_y += cellHeight;
        display.drawFastHLine(0, max_y, display.width(), GxEPD_BLACK);
    }
    if (eveningClassCount > 0) {
        max_y += 2;
        display.drawFastHLine(0, max_y, display.width(), GxEPD_BLACK);
    }
    for (int x = 1; x <= eveningClassCount; x++) {
        max_y += cellHeight;
        display.drawFastHLine(0, max_y, display.width(), GxEPD_BLACK);
    }
    if (nightClassCount > 0) {
        max_y += 2;
        display.drawFastHLine(0, max_y, display.width(), GxEPD_BLACK);
    }
    for (int x = 1; x <= nightClassCount; x++) {
        max_y += cellHeight;
        display.drawFastHLine(0, max_y, display.width(), GxEPD_BLACK);
    }
    for (int x = 1; x < daysCount; x++) { // 垂直分割线
        display.drawFastVLine(cellWidth * x, calLayout.headerH + calLayout.topH, max_y - calLayout.headerH - calLayout.topH, GxEPD_BLACK);
    }

    for (int x = 0; x < daysCount; x++) {
        // Serial.println(daysClassStr[x].c_str());
        int pos_begin = 0;
        int yStep = 0;
        int segmentIndex = 0;


        uint16_t fontColor = GxEPD_BLACK;
        do {
            int pos_end = daysClassStr[x].indexOf(",", pos_begin);
            String ss = daysClassStr[x].substring(pos_begin, pos_end);
            ss.trim();
            // Serial.printf("%s\r\n", ss.c_str());
            if (yStep == 0) {
                if (week_str[tmInfo.tm_wday] == ss) {
                    display.fillRect(cellWidth * x + marginLeft, calLayout.topH, cellWidth, calLayout.headerH, GxEPD_RED);
                    fontColor = GxEPD_RED;
                    u8g2Fonts.setBackgroundColor(GxEPD_RED);
                    u8g2Fonts.setForegroundColor(GxEPD_WHITE);
                } else {
                    fontColor = GxEPD_BLACK;
                    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
                    u8g2Fonts.setForegroundColor(fontColor);
                }
                u8g2Fonts.setFont(FONT_TEXT);
                u8g2Fonts.drawUTF8(cellWidth * x + marginLeft + (cellWidth - u8g2Fonts.getUTF8Width(ss.c_str())) / 2, calLayout.topH + calLayout.headerH - 4, ss.c_str());
            } else if (yStep > 0) {
                if (yStep == morningClassCount + 1 || yStep == (morningClassCount + eveningClassCount + 1)) {
                    segmentIndex++;
                }
                u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
                u8g2Fonts.setForegroundColor(fontColor);
                int fontHeight = 12;
                if (cellHeight < 18) {
                    u8g2Fonts.setFont(FONT_SUB);
                    fontHeight = 12;
                } else {
                    u8g2Fonts.setFont(FONT_TEXT);
                    fontHeight = 16;
                }
                u8g2Fonts.drawUTF8(cellWidth * x + marginLeft + (cellWidth - u8g2Fonts.getUTF8Width(ss.c_str())) / 2, calLayout.topH + calLayout.headerH + cellHeight * yStep - 1 - (cellHeight - 1 - fontHeight) / 2 + 2 * segmentIndex, ss.c_str());
            }

            if (pos_end < 0) break;
            pos_begin = pos_end + 1;
            yStep++;
        } while (true);
    }

}

///////////// Calendar //////////////
/**
 * 处理日历信息
 */
void si_calendar() {
    _calendar_status = 0;

    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    int32_t _calendar_date = pref.getInt(PREF_SI_CAL_DATE);
    _cd_day_label = pref.getString(PREF_CD_DAY_LABLE);
    _cd_day_date = pref.getString(PREF_CD_DAY_DATE);
    _tag_days_str = pref.getString(PREF_TAG_DAYS);
    _week_1st = pref.getString(PREF_SI_WEEK_1ST, "0").toInt();
    _study_schedule = pref.getString(PREF_STUDY_SCHEDULE);
    _si_type = pref.getInt(PREF_SI_TYPE);
    if (_study_schedule.isEmpty()) _si_type = 0;

    pref.end();

    time_t now = 0;
    time(&now);
    localtime_r(&now, &tmInfo); // 时间戳转化为本地时间结构
    Serial.printf("System Time: %d-%02d-%02d %02d:%02d:%02d\n", (tmInfo.tm_year + 1900), tmInfo.tm_mon + 1, tmInfo.tm_mday, tmInfo.tm_hour, tmInfo.tm_min, tmInfo.tm_sec);

    // 如果当前时间无效
    if (tmInfo.tm_year + 1900 < 2025) {
        bool isSetOK = false;
        if (weather_status() == 1) {
            // 尝试使用api获取的时间
            String apiTime;
            Weather* weatherNow = weather_data_now();
            if (weatherNow->updateTime == NULL) {
                // TODO 处理每日天气
                DailyForecast* wFc = weather_data_daily();
                apiTime = wFc->updateTime;
            } else {
                apiTime = weatherNow->updateTime;
            }
            Serial.printf("API Time: %s\n", apiTime.c_str());
            tmInfo = { 0 }; // 重置为0
            if (strptime(apiTime.c_str(), "%Y-%m-%dT%H:%M", &tmInfo) != NULL) {  // 将时间字符串转成tm时间 e.g. 2024-11-14T17:36+08:00
                time_t set = mktime(&tmInfo);
                timeval tv;
                tv.tv_sec = set;
                isSetOK = (settimeofday(&tv, nullptr) == 0);
                Serial.println("WARN: Set system time by api time.");
            } else {
                Serial.println("ERR: Fail to format api time.");
            }
        } else {
            // 如果天气也未获取成功，那么返回
            Serial.println("ERR: invalid time & not weather info got.");
        }
        if (!isSetOK) {
            _calendar_status = 2;
            return;
        }
    }

    char buffer[64];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmInfo);
    Serial.printf("Calendar Show Time: %s\n", buffer);

    nl_month_days(tmInfo.tm_year + 1900, tmInfo.tm_mon + 1, lunarDates);
    nl_year_jq(tmInfo.tm_year + 1900, jqAccDate);

    _calendar_status = 1;
    return;
}

int si_calendar_status() {
    return _calendar_status;
}

///////////// Screen //////////////
/**
 * 屏幕刷新
 */
void task_screen(void* param) {
    Serial.println("[Task] screen update begin...");

    voltage = readBatteryVoltage();

    // 读取刷新模式偏好
    Preferences prefRM;
    prefRM.begin(PREF_NAMESPACE, true);
    _refresh_mode = prefRM.getUChar(PREF_REFRESH_MODE, 0);
    prefRM.end();
    Serial.printf("[Task] refresh mode: %s\n", _refresh_mode == 1 ? "LUT" : "FULL");

    delay(100);

    display.init(115200);
    display.setRotation(ROTATION);
    u8g2Fonts.begin(display);

    display.setFullWindow();
    display.firstPage();
    display.fillScreen(GxEPD_WHITE);
    do {
#if 0 // 旧的 token-only 调试界面；保留以便排查显示问题。
        // ===== 上半部分 (y=0~150)：红色背景 + 白字 =====
        display.fillRect(0, 0, 400, 150, GxEPD_RED);
        u8g2Fonts.setFontMode(1);
        u8g2Fonts.setFontDirection(0);
        u8g2Fonts.setBackgroundColor(GxEPD_RED);
        u8g2Fonts.setForegroundColor(GxEPD_WHITE);
        u8g2Fonts.setFont(u8g2_font_fub25_tn);
        u8g2Fonts.drawUTF8(80, 90, "日历区域");

        // ===== 分隔线 =====
        display.fillRect(0, 150, 400, 4, GxEPD_BLACK);

        // ===== 下半部分 (y=154~300)：DeepSeek 数据 + 条形图 =====
        u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);

        // 标题
        u8g2Fonts.setFont(FONT_TEXT);
        u8g2Fonts.drawUTF8(5, 172, "DeepSeek API 用量");

        if (deepseek_status() == 1) {
            DeepSeekData* ds = deepseek_data();
            uint32_t flashT = ds->tokensTodayFlash;
            uint32_t proT   = ds->tokensTodayPro;
            uint32_t maxV = max(flashT, proT);
            if (maxV == 0) maxV = 1;

            int barBaseY = 270;
            int barH = 50;

            // Flash 标签 + 数字
            u8g2Fonts.setFont(FONT_SUB);
            u8g2Fonts.setForegroundColor(GxEPD_BLACK);
            char buf[40];
            snprintf(buf, sizeof(buf), "Flash: %u tokens (%u次)", flashT, flashT/32+1);
            u8g2Fonts.drawUTF8(5, 188, buf);

            // Flash 条形图（红色）
            int flashW = (int)((float)flashT / maxV * 350);
            if (flashW < 10) flashW = 10;
            display.fillRect(5, 195, flashW, 18, GxEPD_RED);
            u8g2Fonts.setForegroundColor(GxEPD_WHITE);
            snprintf(buf, sizeof(buf), "%u", flashT);
            u8g2Fonts.drawUTF8(10, 210, buf);

            // Pro 标签 + 数字
            u8g2Fonts.setForegroundColor(GxEPD_BLACK);
            snprintf(buf, sizeof(buf), "Pro:   %u tokens (%u次)", proT, proT/32+1);
            u8g2Fonts.drawUTF8(5, 230, buf);

            // Pro 条形图（黑色）
            int proW = (int)((float)proT / maxV * 350);
            if (proW < 10) proW = 10;
            display.fillRect(5, 237, proW, 18, GxEPD_BLACK);
            u8g2Fonts.setForegroundColor(GxEPD_WHITE);
            snprintf(buf, sizeof(buf), "%u", proT);
            u8g2Fonts.drawUTF8(10, 252, buf);

            // 余额
            u8g2Fonts.setForegroundColor(GxEPD_BLACK);
            u8g2Fonts.setFont(FONT_SUB);
            snprintf(buf, sizeof(buf), "余额: %s %s", ds->currency.c_str(), ds->totalBalance.c_str());
            u8g2Fonts.drawUTF8(5, 282, buf);
        } else {
            u8g2Fonts.setFont(FONT_TEXT);
            u8g2Fonts.setForegroundColor(GxEPD_RED);
            u8g2Fonts.drawUTF8(10, 220, "未配置 DeepSeek Key");
        }

#else
        // 日历为主视图；Token/余额和电量显示在右上状态区。
        display.fillScreen(GxEPD_WHITE);
        draw_compact_calendar();
        draw_deepseek_token_panel();
#endif
    } while (display.nextPage());

    int32_t _calendar_date = (tmInfo.tm_year + 1900) * 10000 + (tmInfo.tm_mon + 1) * 100 + tmInfo.tm_mday;

    Preferences pref;
    pref.begin(PREF_NAMESPACE);
    pref.putInt(PREF_SI_CAL_DATE, _calendar_date);
    // 记录今日已完成全屏刷新，重置局部刷新计数
    char todayBuf[9];
    snprintf(todayBuf, sizeof(todayBuf), "%04d%02d%02d", tmInfo.tm_year + 1900, tmInfo.tm_mon + 1, tmInfo.tm_mday);
    pref.putString(PREF_FULL_DATE, String(todayBuf));
    pref.putUChar(PREF_PARTIAL_CNT, 0);
    pref.end();

    display.powerOff();
    display.hibernate();
    Serial.println("[Task] screen update end...");

    _screen_status = 1;
    SCREEN_HANDLER = NULL;
    vTaskDelete(NULL);
}

void si_screen() {
    _screen_status = 0;
    si_calendar(); // 准备日历数据

    if (si_calendar_status() == 2) {
        Serial.println("ERR: System time prepare failed.");
        _screen_status = 2;
        return;
    }

    if (SCREEN_HANDLER != NULL) {
        vTaskDelete(SCREEN_HANDLER);
    }
    xTaskCreate(task_screen, "Screen", 4096, NULL, 2, &SCREEN_HANDLER);
}

int si_screen_status() {
    return _screen_status;
}

/**
 * 仅局部刷新状态栏（DeepSeek token + 电池），避免全屏闪屏
 * 使用 LUT 差分刷新，不改变日历/天气区域
 */
void si_screen_partial_status() {
    _screen_status = 0;
    Serial.println("[Task] screen partial status update begin...");

    voltage = readBatteryVoltage();

    // 使用与全刷一致的初始化，局部窗口由 draw_status(true) 内部设置
    display.init(115200);
    display.setRotation(ROTATION);
    u8g2Fonts.begin(display);

    draw_status(true); // partial=true

    display.powerOff();
    display.hibernate();
    Serial.println("[Task] screen partial status update end...");

    // 更新局部刷新计数，每 24 次做一次全刷防残影
    Preferences pref;
    pref.begin(PREF_NAMESPACE, false);
    uint8_t cnt = pref.getUChar(PREF_PARTIAL_CNT, 0) + 1;
    if (cnt >= 24) {
        cnt = 0;
        // 标记需要全刷：清除 FULL_DATE 强制下次全刷
        pref.putString(PREF_FULL_DATE, "");
        Serial.println("[Task] partial refresh limit reached, next will be full refresh.");
    }
    pref.putUChar(PREF_PARTIAL_CNT, cnt);
    pref.end();

    _screen_status = 1;
}

void print_status() {
    Serial.printf("Weather: %d\n", weather_status());
    Serial.printf("Calendar: %d\n", si_calendar_status());
    Serial.printf("Screen: %d\n", si_screen_status());
}



void si_warning(const char* str) {
    Serial.println("Screen warning...");
    display.init(115200);          // 串口使能 初始化完全刷新使能 复位时间 ret上拉使能
    display.setRotation(ROTATION); // 设置屏幕旋转1和3是横向  0和2是纵向
    u8g2Fonts.begin(display);

    display.setFullWindow();
    display.fillScreen(GxEPD_WHITE);
    do {
        u8g2Fonts.setFontMode(1);
        u8g2Fonts.setFontDirection(0);
        u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);

        u8g2Fonts.setFont(u8g2_font_open_iconic_all_4x_t);
        int space = 8;
        int w = u8g2Fonts.getUTF8Width("\u0118") + space;
        u8g2Fonts.setFont(FONT_TEXT);
        w += u8g2Fonts.getUTF8Width(str);

        u8g2Fonts.setForegroundColor(GxEPD_RED);
        u8g2Fonts.setFont(u8g2_font_open_iconic_all_4x_t);
        u8g2Fonts.setCursor((display.width() - w) / 2, (display.height() + 32) / 2);
        u8g2Fonts.print("\u0118");

        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        u8g2Fonts.setCursor(u8g2Fonts.getCursorX() + space, u8g2Fonts.getCursorY() - 8);
        u8g2Fonts.setFont(FONT_TEXT);
        u8g2Fonts.print(str);
    } while (display.nextPage());

    display.powerOff();
    display.hibernate();
}
