#ifndef HW_CONF_H
#define HW_CONF_H

#include <stdint.h>
#include <unistd.h>
#include <stdatomic.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/*--------------------------- ATTENUATOR GPIO CONTROL DEFINITIONS ------------------------*/

#define GPIO_CHIP "gpiochip0"
#define GPIO_LINE 23   // Attenuation GPIO

typedef struct tx_ctx tx_ctx_t;


void gpio_cleanup(tx_ctx_t *ctx);
void tx_chip_close(tx_ctx_t *ctx);
void tx_line_close(tx_ctx_t *ctx);
int tx_chip_init(tx_ctx_t *ctx);
int tx_line_init(tx_ctx_t *ctx, unsigned int gpio_line);
int tx_req_out(tx_ctx_t *ctx, const char *consumer_label, int idle_value);
int tx_req_in(tx_ctx_t *ctx, const char *consumer_label);
int tx_clr_bit(tx_ctx_t *ctx);
void gpio_in(tx_ctx_t *txt);
void gpio_out(tx_ctx_t *txt);
void gpio_clr(tx_ctx_t *txt);
void tx_send(uint8_t state, tx_ctx_t *txt);

/*--------------------------- ATTENUATOR GPIO CONTROL DEFINITIONS ------------------------*/




/*--------------------------- PIO CARRIER GPIO CONTROL DEFINITIONS ------------------------*/

#define TARGET_HZ 77500.0
#define CLOCK_FREQ 200000000.0
#define CARRIER_PIN 18U

// PIO Assembly, Compatible with RP2040 Instruction Set: Datasheet - https://pip-assets.raspberrypi.com/categories/814-rp2040/documents/RP-008371-DS-1-rp2040-datasheet.pdf Section 3.4
extern const uint16_t carrier_freq_program_instructions[];

typedef struct clk_vals{
    uint32_t loops;
    double clk_div_ideal;
    double clk_div_real;
    double f_actual;
    double err;
} clk_vals_t;


struct pio_instance;
struct pio_program;

void pio_cleanup(struct pio_instance **pio_driver,int *sm_r, uint *sm_off,  struct pio_program prog);
clk_vals_t find_best(double f_pio, double f_target, uint32_t Loops_min, uint32_t Loops_max);

/*--------------------------- PIO CARRIER GPIO CONTROL DEFINITIONS ------------------------*/




/*--------------------------- THREAD  DEFINITIONS ------------------------*/

void clk_delay(struct timespec tm);
void  thread_setup(pthread_t thread, int core);
void* carrier_conf(void *args);
void* data_tx(void *args);

/*--------------------------- THREAD  DEFINITIONS ------------------------*/

#ifdef __cplusplus
}
#endif

#endif