#include "rtc.h"

#define CMOS_ADDR   0x70
#define CMOS_DATA   0x71

#define RTC_SECOND  0x00
#define RTC_MINUTE  0x02
#define RTC_HOUR    0x04
#define RTC_DAY     0x07
#define RTC_MONTH   0x08
#define RTC_YEAR    0x09
#define RTC_CENTURY 0x32
#define RTC_STATUS_A 0x0A
#define RTC_STATUS_B 0x0B

static void cmos_delay(void) {

    inb(0x80);
}

int rtc_is_updating(void) {
    outb(CMOS_ADDR, RTC_STATUS_A);
    cmos_delay();
    return (inb(CMOS_DATA) & 0x80) != 0;
}

static u8 rtc_read_reg(u8 reg) {
    outb(CMOS_ADDR, reg);
    cmos_delay();
    return inb(CMOS_DATA);
}

u8 bcd_to_bin(u8 bcd) {
    return ((bcd >> 4) * 10) + (bcd & 0x0F);
}

static int rtc_is_binary(void) {
    outb(CMOS_ADDR, RTC_STATUS_B);
    cmos_delay();
    return (inb(CMOS_DATA) & 0x04) != 0;
}

static int rtc_is_24hour(void) {
    outb(CMOS_ADDR, RTC_STATUS_B);
    cmos_delay();
    return (inb(CMOS_DATA) & 0x02) != 0;
}

void rtc_read(rtc_time_t* t) {
    if (!t) return;

    while (rtc_is_updating()) {

    }

    int binary = rtc_is_binary();

    u8 sec = rtc_read_reg(RTC_SECOND);
    u8 min = rtc_read_reg(RTC_MINUTE);
    u8 hour = rtc_read_reg(RTC_HOUR);
    u8 day = rtc_read_reg(RTC_DAY);
    u8 month = rtc_read_reg(RTC_MONTH);
    u8 year = rtc_read_reg(RTC_YEAR);

    if (!binary) {
        sec = bcd_to_bin(sec);
        min = bcd_to_bin(min);
        hour = bcd_to_bin(hour);
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = bcd_to_bin(year);
    }

    if (!rtc_is_24hour()) {

        int pm = hour & 0x80;
        hour = hour & 0x7F;
        if (!binary) {
            hour = bcd_to_bin((u8)hour);
        }
        if (pm && hour < 12) {
            hour += 12;
        }
        if (!pm && hour == 12) {
            hour = 0;
        }
    }

    t->second = sec;
    t->minute = min;
    t->hour = hour;
    t->day = day;
    t->month = month;
    t->year = year;
    t->century = 20;
}

void rtc_get_time_string(char* buf, int size, int show_seconds, int use_12_hour) {
    int h, pm;
    if (!buf || size < 6) return;

    rtc_time_t t;
    rtc_read(&t);

    h = (int)t.hour;
    pm = 0;
    if (use_12_hour) {
        pm = (h >= 12);
        if (h == 0) {
            h = 12;
        } else if (h > 12) {
            h -= 12;
        }
    }

    if (show_seconds && size >= 9) {

        buf[0] = '0' + (h / 10);
        buf[1] = '0' + (h % 10);
        buf[2] = ':';
        buf[3] = '0' + (t.minute / 10);
        buf[4] = '0' + (t.minute % 10);
        buf[5] = ':';
        buf[6] = '0' + (t.second / 10);
        buf[7] = '0' + (t.second % 10);
        buf[8] = '\0';
    } else {

        buf[0] = '0' + (h / 10);
        buf[1] = '0' + (h % 10);
        buf[2] = ':';
        buf[3] = '0' + (t.minute / 10);
        buf[4] = '0' + (t.minute % 10);
        buf[5] = '\0';
    }

    if (use_12_hour) {
        int base = show_seconds ? 8 : 5;
        if (base + 3 < size) {
            buf[base++] = ' ';
            buf[base++] = pm ? 'P' : 'A';
            buf[base++] = 'M';
            buf[base] = 0;
        }
    }
}

void rtc_format_short_date(char* buf, int size) {
    rtc_time_t t;
    int m, d, yy, i;
    if (!buf || size < 10) return;
    rtc_read(&t);
    m = (int)t.month;
    d = (int)t.day;
    yy = (int)t.year % 100;
    i = 0;
    buf[i++] = (char)('0' + m / 10);
    buf[i++] = (char)('0' + m % 10);
    buf[i++] = '/';
    buf[i++] = (char)('0' + d / 10);
    buf[i++] = (char)('0' + d % 10);
    buf[i++] = '/';
    buf[i++] = (char)('0' + yy / 10);
    buf[i++] = (char)('0' + yy % 10);
    buf[i] = 0;
}
