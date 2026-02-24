#ifndef DCF77_H
#define DCF77_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bit_mod {
    uint8_t state;
    int duration;
} bit_mod_t;

typedef struct DSTRule{
    int month;     // 1-12
    int week;      // 5 = last week
    int dayOfWeek; // 0 = Sunday
    int hour;      // Local hour of change
    int mday;
} DSTRule_t;

#define Carrier_MOD 0x0
#define Carrier 0x1

extern uint64_t minute_frame;

extern DSTRule_t startRule;
extern DSTRule_t endRule;

// Sakamoto last Sunday math: Returns the date of the last Sunday of a given month
int weekday(int y, int m, int d);
int get_last_sunday(int year, int month);

//DST EU date switch calculator
void calculate_dst_flag(struct tm *t);

//Flag toggle 1hr before event
uint8_t toggle_calc(struct tm* t);
//Leap second toggle
uint8_t leap_calc(int hr, uint8_t active);

uint64_t bcd_conv(uint8_t n);

uint64_t even_parity(int start, int end);

bit_mod_t set_modulation(int bit_second);

void tx_block_prep (time_t t_min, uint8_t *ntp_leap);

#ifdef __cplusplus
}
#endif

#endif