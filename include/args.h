#ifndef ARGS_H
#define ARGS_H

#include <stdatomic.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
    
#define SET_LOCAL 0x0
#define SET_NTP 0x1

typedef struct parser{
    uint8_t t_src; 
    int t_lim;
    int t_toff;
    uint8_t verbose;
    atomic_bool *stop_thread;
    pthread_mutex_t *lck;
    pthread_cond_t *ext;
    int *exit;
} parser_t;

int parse_arguments(int argc, char *argv[], parser_t *out);

void verbose_time(time_t t);

#ifdef __cplusplus
}
#endif

#endif // ARGS_H