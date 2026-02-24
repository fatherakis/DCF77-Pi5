#include <time.h>
#include "dcf77.h"

uint64_t minute_frame = 0;
// European Union Rules: Berlin - CET-1CEST,M3.5.0/2,M10.5.0/3
DSTRule_t startRule = {3, 5, 0, 2, 0}; // Last Sun in March at 2am
DSTRule_t endRule =   {10, 5, 0, 3, 0}; // Last Sun in Oct at 3am

int weekday(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y--;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

// Sakamoto last Sunday math: Returns the date of the last Sunday of a given month
int get_last_sunday(int year, int month) {
    static const int mdays[] = {
        31,28,31,30,31,30,31,31,30,31,30,31
    };

    int days = mdays[month - 1];

    /* Leap year adjustment */
    if (month == 2) {
        int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        days += leap;
    }

    int w = weekday(year, month, days);  // weekday of last day
    return days - w;                     // back to Sunday
}

void calculate_dst_flag(struct tm *t) {
    // Logic Check
    int year = t->tm_year + 2000; //Year sent is on 0 - 99 scale
    
    //DST Always happens on last sunday of March && October
    int last_sun_march = get_last_sunday(year , startRule.month);
    int last_sun_oct = get_last_sunday(year, endRule.month);

    //printf("Start: March %d, End: Oct %d \n", last_sun_march, last_sun_oct);

    startRule.mday = (uint8_t) last_sun_march;
    endRule.mday = (uint8_t) last_sun_oct;
}

uint8_t toggle_calc(struct tm* t){
    //When the DST start day & month occurs, return 1 only if it's within 1hr before the event (happens on 02:00)
    if (t->tm_mon == startRule.month && t->tm_mday == startRule.mday && t->tm_hour == startRule.hour-1) return 1;
    //When the DST end day & month occurs, return 1 only if it's within 1hr before the event (happens on 03:00)
    if (t->tm_mon == endRule.month && t->tm_mday == endRule.mday && t->tm_hour == endRule.hour-1) return 1;
    return 0;
}

uint8_t leap_calc(int hr, uint8_t active){
    if (active && hr == 23) return 1;
    else return 0;
}

uint64_t bcd_conv(uint8_t n) { return ( ( (n / 10) % 10) << 4 ) | ( n % 10 ); }

uint64_t even_parity(int start, int end){
    uint64_t parity = 0;
    for (int i = start; i <= end; ++i){ parity ^= (minute_frame >> i) & 1ULL; }
    return parity & 0x1;
}

bit_mod_t set_modulation(int bit_sec){
    if (bit_sec == 59 && (minute_frame & (1ULL << 60))) return (bit_mod_t){Carrier_MOD, 100};
    if (bit_sec > 58) return (bit_mod_t){Carrier, 0};
    return (bit_mod_t){Carrier_MOD, (minute_frame & (1ULL << bit_sec)) ? 200 : 100};
}

void tx_block_prep(time_t t_min, uint8_t *ntp_leap){
    t_min += 60;
    struct tm tx_time;
    localtime_r(&t_min, &tx_time);
    //Value corrections
    tx_time.tm_mon += 1;
    tx_time.tm_wday = tx_time.tm_wday == 0 ? 7 : tx_time.tm_wday;
    tx_time.tm_year = tx_time.tm_year % 100; // year tm starts from 1900. DCF77 expects 0-99 within century.
    uint8_t leap_sec = *ntp_leap ? 0 : leap_calc(tx_time.tm_hour, *ntp_leap);
    calculate_dst_flag(&tx_time);
    uint8_t dst_toggle = toggle_calc(&tx_time);

    minute_frame = 0;

    minute_frame |= bcd_conv(tx_time.tm_year)  << 50;
    minute_frame |= bcd_conv(tx_time.tm_mon) << 45;
    minute_frame |= bcd_conv(tx_time.tm_wday) << 42;
    minute_frame |= bcd_conv(tx_time.tm_mday) << 36;
    minute_frame |= bcd_conv(tx_time.tm_hour) << 29;
    minute_frame |= bcd_conv(tx_time.tm_min) << 21;
    minute_frame |= 1ULL << 20;
    minute_frame |= bcd_conv(leap_sec) << 19;
    minute_frame |= bcd_conv(tx_time.tm_isdst ? 0 : 1) << 18;
    minute_frame |= bcd_conv(tx_time.tm_isdst) << 17;
    minute_frame |= bcd_conv(dst_toggle) << 16;

    minute_frame |= even_parity(36, 57) << 58;
    minute_frame |= even_parity(29, 34) << 35;
    minute_frame |= even_parity(21, 27) << 28;


    if (tx_time.tm_hour == 23 && tx_time.tm_min == 59 && leap_sec) minute_frame |= 1ULL << 60; // Unused 60th position as 23:59:00 leap second trigger
    return;
}