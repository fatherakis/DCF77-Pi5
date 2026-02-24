#include <piolib/piolib.h>
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <piolib/piolib.h>

#include "hw_conf.h"
#include "args.h"

const uint16_t carrier_freq_program_instructions[] = {
    //      .wrap
    0xe001, //  0:  set PINS, 1
    0xa026, //  1:  mov X, ISR
    0x0042, //  2:  jmp X--, 2     (High State Delay)
    0xe000, //  3:  set PINS, 0
    0xa026, //  4:  mov X, ISR
    0x0045 //  5:  jmp X--, 5     (Low State Delay)
    //      .wrap
};

clk_vals_t find_best(double f_pio, double f_target, uint32_t Loops_min, uint32_t Loops_max){
    clk_vals_t vals = {0};
    vals.err = DBL_MAX; //FP Max

    for (uint32_t L = Loops_min; L <= Loops_max; ++L) {
        double Cycle_per_Period = 2.0 * ((double)L + 3.0);
        double clk_div_ideal = f_pio / (f_target * Cycle_per_Period);

        //Divider value limits
        if (clk_div_ideal < 1.0 || clk_div_ideal > 65535.0) continue;

        //precision allowed up to 1/256
        double clk_div_real = round(clk_div_ideal * 256.0) / 256.0;

        double f_real = f_pio / (clk_div_real * Cycle_per_Period);
        double err = fabs(f_real - f_target);

        if (err <= vals.err) {
            vals.loops = L;
            vals.clk_div_ideal = clk_div_ideal;
            vals.clk_div_real = clk_div_real;
            vals.f_actual = f_real;
            vals.err = err;
        }
    }
    return vals;
}


void pio_cleanup(struct pio_instance **pio_driver,int *sm_r, uint *sm_off, struct pio_program prog){
    fprintf(stderr, "Shutting down PIO carrier\n");
    if (*sm_r >= 0) {
        pio_sm_set_enabled(*pio_driver, *sm_r, false);
        pio_sm_unclaim(*pio_driver, *sm_r);
    }

    if (*sm_off) {
        pio_remove_program(*pio_driver, &prog, *sm_off);
    }
}


void* carrier_conf(void *args) {
    thread_setup(pthread_self(), 1);
    
    parser_t* t_args = (parser_t *)args;
    clk_vals_t clock_settings = find_best(CLOCK_FREQ, TARGET_HZ, 30, 800);
    if (t_args->verbose) fprintf(stderr, "Carrier config: GPIO=%u Clock=%.0f MHz \nFrequency requested=%.1fHz loop_count=%u clkdiv=%.9f\n", CARRIER_PIN, CLOCK_FREQ/1000000, clock_settings.f_actual,  clock_settings.loops, clock_settings.clk_div_real);
    
    if (pio_init() < 0) { perror("pio_init"); return NULL; }
    
    const struct pio_program carrier_program = {
        .instructions = carrier_freq_program_instructions,
        .length = 6,
        .origin = -1
    };


    // PIO definitions: driver, state_machine, memory_offset
    PIO g_pio = NULL;
    int g_sm = -1;
    uint g_offset = 0;

    g_pio = pio0;
    g_sm = pio_claim_unused_sm(g_pio, true);
    g_offset = pio_add_program(g_pio, &carrier_program);

    // GPIO routing
    pio_gpio_init(g_pio, CARRIER_PIN);
    pio_sm_set_consecutive_pindirs(g_pio, g_sm, CARRIER_PIN, 1, true);
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_set_pins(&c, CARRIER_PIN, 1);

    // Wrap program for continuous run
    sm_config_set_wrap(&c, g_offset + 0, g_offset + 5);

    sm_config_set_clkdiv(&c, (float)clock_settings.clk_div_real);

    pio_sm_init(g_pio, g_sm, g_offset, &c);
    pio_sm_set_enabled(g_pio, g_sm, false);
    pio_sm_put_blocking(g_pio, g_sm, clock_settings.loops);
    pio_sm_exec(g_pio, g_sm, pio_encode_pull(false, true));
    pio_sm_exec(g_pio, g_sm, pio_encode_mov(pio_isr, pio_osr));
    pio_sm_set_enabled(g_pio, g_sm, true);
    
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000L }; // 1ms
    while (!atomic_load_explicit(t_args->stop_thread, memory_order_acquire)) nanosleep(&ts, NULL);
    pio_cleanup(&g_pio, &g_sm, &g_offset, carrier_program);

    pthread_mutex_lock(t_args->lck);
    if (*t_args->exit == 0) {
        *t_args->exit = 2;
        pthread_cond_signal(t_args->ext);
    }
    pthread_mutex_unlock(t_args->lck);
    return NULL;
}
