#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <gpiod.h>

#include "hw_conf.h"
#include "net_ntp.h"
#include "args.h"
#include "dcf77.h"

struct tx_ctx {
    struct gpiod_chip *chip;
    struct gpiod_line *line;
    int mode; // 0 = Hi-Z (input), 1 = drive LOW (output low)
};

void clk_delay(struct timespec tm) {
    while (clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &tm, NULL) == EINTR) { fprintf(stderr, "Interrupted clk sleep\n"); }
}

void gpio_cleanup(tx_ctx_t *ctx){
    fprintf(stderr, "Shutting down GPIO carrier\n");
    tx_line_close(ctx);
    tx_chip_close(ctx);
}

int tx_chip_init(tx_ctx_t *ctx){
    if (!ctx || !GPIO_CHIP) {
        errno = EINVAL;
        return -1;
    }

    memset(ctx, 0, sizeof(*ctx));
    ctx-> mode = 0;
    ctx->chip = gpiod_chip_open_by_name(GPIO_CHIP);
    if (!ctx->chip) {
        perror("gpiod_chip_open_by_name");
        tx_chip_close(ctx);
        return -1;
    }
    return 1;
}

int tx_line_init(tx_ctx_t *ctx, unsigned int gpio_line){
    
    ctx->line = gpiod_chip_get_line(ctx->chip, (unsigned int)gpio_line);
    if (!ctx->line) {
        perror("gpiod_chip_get_line");
        tx_line_close(ctx);
        return -1;
    }
    return 1;
}

void tx_chip_close(tx_ctx_t *ctx){
    if (!ctx) return;
    
    if (ctx->line) {
        gpiod_line_release(ctx->line);
        ctx->line = NULL;
    }
    if (ctx->chip) {
        gpiod_chip_close(ctx->chip);
        ctx->chip = NULL;
    }
    ctx->mode = 0;
}

void tx_line_close(tx_ctx_t *ctx){
    if (!ctx) return;
    
    if (ctx->line) {
        gpiod_line_release(ctx->line);
        ctx->line = NULL;
    }
}

int tx_req_out(tx_ctx_t *ctx, const char *consumer_label, int idle_value){
    if (gpiod_line_request_output(ctx->line, consumer_label, idle_value) < 0) {
        perror("gpiod_line_request_output");
        tx_line_close(ctx);
        return -1;
    }
    ctx->mode = 1;
    return 1;
}

int tx_req_in(tx_ctx_t *ctx, const char *consumer_label){
    if (gpiod_line_request_input(ctx->line, consumer_label) < 0) {
        perror("gpiod_line_request_input");
        tx_line_close(ctx);
        return -1;
    }
    ctx->mode = 0;
    return 1;
}

int tx_clr_bit(tx_ctx_t *ctx){
    // "Clear" == drive low
    if (gpiod_line_set_value(ctx->line, 0) < 0) {
        perror("gpiod_line_set_value(0)");
    }
    tx_line_close(ctx);
    return 1;
}


void gpio_in(tx_ctx_t *txt){
    if (tx_line_init(txt, GPIO_LINE) < 0) {
        perror("In Req: Line init failed");
    }
    if (tx_req_in(txt, "dcf77-high_z") < 0) {
        perror("In Req: High-Z mode failed");
    }
}

void gpio_out(tx_ctx_t *txt){
    if (tx_line_init(txt, GPIO_LINE) < 0) {
        perror("Out Req: Line init failed");
    }
    
    if (tx_req_in(txt, "dcf77-high_z") < 0) {
        perror("Out Req: High-Z mode failed");
    }
    
    tx_line_close(txt);
    tx_line_init(txt, GPIO_LINE);
    
    if (tx_req_out(txt, "dcf77-gnd", 0) < 0) {
        perror("Out Req: Out mode failed");
    }
}

void gpio_clr(tx_ctx_t *txt){
    tx_clr_bit(txt);
}

void tx_send(uint8_t state, tx_ctx_t *txt){
    if (state){
        gpio_out(txt);
        gpio_clr(txt);
    }
    else{
        gpio_in(txt);
        tx_line_close(txt);
    }
    return;
}


void thread_setup(pthread_t thread, int core){
    // CPU Affinity on specific core
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core, &cpuset); // RPi5 is quad-core

    if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) != 0) {
        perror("pthread_setaffinity_np");
    }
    // Thread priority
    struct sched_param param;
    param.sched_priority = 99; // High priority (1-99)
    pthread_setschedparam(thread, SCHED_FIFO, &param);
}

void* data_tx(void *args){
    thread_setup(pthread_self(), 3);

    tx_ctx_t gpio_driver;
    if (tx_chip_init(&gpio_driver) < 0){
        perror("GPIO chip init failed");
    }

    parser_t* t_args = (parser_t *)args;


    int timeout =  (t_args->t_lim > 0) ? t_args->t_lim : 960; //Default timeout after 16hrs -> 960 mins
    
    time_t start_transm;
    uint8_t leap;
    
    if (!t_args->t_src){ const time_t start_time = time(NULL); start_transm = start_time; leap = 0;}
    if (t_args->t_src){ ret_ntp *sync = ntp_get(); start_transm = sync->time_data; leap = sync->leap_sec;}
    start_transm -= start_transm % 60; //Round down to the minute
    start_transm += t_args->t_toff * (time_t)60;

    struct timespec sec_swi;
    for (time_t minute_start = start_transm; timeout-- && !atomic_load_explicit(t_args->stop_thread, memory_order_acquire); minute_start += 60) {
        if (t_args->verbose) verbose_time(minute_start);

        tx_block_prep(minute_start, &leap);

        for (int sec = 0; sec < 60 && !atomic_load_explicit(t_args->stop_thread, memory_order_acquire); sec++) {
            const bit_mod_t modulation = set_modulation(sec);
            
            sec_swi.tv_sec = minute_start + sec; // Wait until second turnover
            sec_swi.tv_nsec = 0;
            clk_delay(sec_swi);

            if (t_args->verbose) fprintf(stderr, "\b\b\b:%02d", sec);    
        
            tx_send(modulation.state, &gpio_driver);
            if (modulation.duration == 0) continue; //Last segment, skip ms delay
            sec_swi.tv_nsec += modulation.duration * 1000000L;
            clk_delay(sec_swi);

            if (sec == 59 && modulation.duration != 0){
                //Leap second logic: 
                //  1. set_mod gave '0' for 59 sec
                //      1a. Leap is set 
                //      1b. we are in 23:59:00 min (leap: 59th = 0, 60th = No MOD)
                //  2. already sent '0' for 59th
                //  3. wait for 1sec (60th leap sec) before moving on next min
                sec_swi.tv_sec++;
                clk_delay(sec_swi);
            }

            tx_send(Carrier, &gpio_driver); //Attenuation Complete; Stop modulation
        }
        fprintf(stderr, "\n");
    }

    gpio_cleanup(&gpio_driver);
    
    pthread_mutex_lock(t_args->lck);
    if (*t_args->exit == 0) {
        *t_args->exit = 2;
        pthread_cond_signal(t_args->ext);
    }
    pthread_mutex_unlock(t_args->lck);
    return NULL;
}