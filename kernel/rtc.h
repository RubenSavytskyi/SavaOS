#ifndef RTC_H
#define RTC_H

#include "types.h"

typedef struct {
    u8 second;
    u8 minute;
    u8 hour;
    u8 day;
    u8 month;
    u8 year;
    u8 century;
} rtc_time_t;

void rtc_read(rtc_time_t* t);

void rtc_get_time_string(char* buf, int size, int show_seconds, int use_12_hour);

void rtc_format_short_date(char* buf, int size);

u8 bcd_to_bin(u8 bcd);

int rtc_is_updating(void);

#endif
