#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <getopt.h>
#include <signal.h>
#include <stdatomic.h>

#include "args.h"
#include "hw_conf.h"
#include "net_ntp.h"
#include "dcf77.h"
#include "version.h"

static atomic_bool stop_thread = 0;
static pthread_mutex_t lck = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  ext = PTHREAD_COND_INITIALIZER;

static int thread_exit = 0;

static void handle_signal(int sig){
    (void)sig;
    atomic_store_explicit(&stop_thread, 1, memory_order_release);
}

/*
+------------------------------------------------------------------------------------------+
|----------------------------------- DCF77 Transmission Frame -----------------------------|
+------------------------------------------------------------------------------------------+ 
00 bit: '0'
01 - 14 bit: Critical Warnings: 0 for this program
15 bit: Call bit (Abnormal state): 0 for this program
16 bit: DST Switch on next hour.
17 bit: '1' for CEST active else '0'.
18 bit: '1' for CET active else '0'.
19 bit: Leap Second on hour end.
20 bit: '1'
21 - 27 bits: Minute [0-59]
28 bit: Even parity on [21 - 27]. 
29 - 34 bits: Hour [0-23]
35 bit: Even parity on [29 - 34]. 
36 - 41 bits: [0-31] Day of the month.
42 - 44 bits: [1-7] Day of the Week.
45 - 49 bits: [01-12] Month Number.
50 - 57 bits: [00-99] Year within century. 
58 bit: Even parity on [36 - 57]
59 bit: Minute mark, disabled modulation. 
!!!!! On Leap second: 59th sec sends '0', 60th '1'. !!!!!
Even Parity: if total '1' are even.
+------------------------------------------------------------------------------------------+
|----------------------------------- DCF77 Transmission Frame -----------------------------|
+------------------------------------------------------------------------------------------+



<------------------------------------------------------------------------------------------------------->
<---------------------------------- struct definition and conversion for DCF77 ------------------------->
<------------------------------------------------------------------------------------------------------->
struct tm {
    int tm_sec;     val ∈ [0 - 60] seconds, 60th second accounts for leap second -- It's not used with ntp approach
    int tm_min;     val ∈ [0 - 59] minutes, used as is
    int tm_hour;    val ∈ [0 - 23] hours, used as is
    int tm_mday;    val ∈ [1 - 31] day of month, used as is
    int tm_mon;     val ∈ [0 - 11] month, Requires + 1 for proper use. DCF77 month val ∈ [1,12]
    int tm_year;    val ∈ [0 - 999] years since 1900. Requires transformation to current century (% 100). DCF77 year val ∈ [00 - 99]
    int tm_wday;    val ∈ [0 - 6] day of week. 0 is Sunday. DCF77 requires 7 for Sunday. Use DAY_LUT lookup table
    int tm_yday;    val ∈ [0 - 365] Not used.
    int tm_isdst;   val ∈ {0 , >0 , <0} 0 for Daylight Saving inactive. <0 for Unknown. >0 For active. !!! INCONSISTENT WHEN USED FROM GMTIME !!! MANUAL CALCULATION PREFERED
};
<------------------------------------------------------------------------------------------------------->
<---------------------------------- struct definition and conversion for DCF77 ------------------------->
<------------------------------------------------------------------------------------------------------->
*/

void verbose_time(time_t t) {
    char buf[32];
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    fputs(buf,stderr);
}

void usage(FILE *out, const char *prog){
    fprintf(out,"Usage: sudo %s -s [local|ntp] [-l minutes] [-o minutes] [-v]\n"
                "Required:\n"
                "  -s, --source   local|ntp   (required)\n"
                "Options:\n"
                "  -l, --limit    minutes     (>0 if provided, Default: 960mins (16 hours))\n"
                "  -o, --offset   minutes     (Transmit offset time from Germany local time)\n"
                "  -v, --verbose\n"
                "  -h, --help                 (This message)\n", prog);
}

static int parse_int10(const char *s, int *out){
    errno = 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);

    if (s == end) return -1;                 // no digits
    if (*end != '\0') return -1;             // trailing junk (rejects "2312.042")
    if (errno == ERANGE) return -1;          // overflow/underflow
    if (v < INT_MIN || v > INT_MAX) return -1;

    *out = (int)v;
    return 0;
}

static int parse_source(const char *s, uint8_t *out){
    if (strcmp(s, "local") == 0) { *out = SET_LOCAL; return 0;}
    if (strcmp(s, "ntp") == 0)   { *out = SET_NTP; return 0;}
    return -1;
}

int parse_arguments(int argc, char *argv[], parser_t *out){
    if (!out) return -1;

    *out = (parser_t){
        .t_src = 0x2,
        .t_lim = 0,
        .t_toff = 0,
        .verbose = 0
    };

    static const struct option longopts[] = {
        {"source",  required_argument, 0, 's'},
        {"limit",   required_argument, 0, 'l'},
        {"offset",  required_argument, 0, 'o'},
        {"verbose", no_argument,       0, 'v'},
        {"help",    no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    optind = 1;

    int c;
    while ((c = getopt_long(argc, argv, "s:l:o:vh", longopts, NULL)) != -1) {
        switch (c) {
            case 's':
                if (parse_source(optarg, &out->t_src) != 0) {
                    fprintf(stderr, "Error: invalid source '%s' (use local|ntp)\n\n", optarg);
                    usage(stderr, argv[0]);
                    return -1;
                }
                break;

            case 'l': {
                int v;
                if (parse_int10(optarg, &v) != 0 || v <= 0) {
                    fprintf(stderr, "Error: invalid limit '%s' (must be integer > 0)\n\n", optarg);
                    usage(stderr, argv[0]);
                    return -1;
                }
                out->t_lim = v;
                break;
            }

            case 'o': {
                int v;
                if (parse_int10(optarg, &v) != 0) {
                    fprintf(stderr, "Error: invalid offset '%s' (must be integer)\n\n", optarg);
                    usage(stderr, argv[0]);
                    return -1;
                }
                out->t_toff = v;
                break;
            }

            case 'v':
                out->verbose = 1;
                break;

            case 'h':
                usage(stdout, argv[0]);
                exit(0);

            default:
                if (optopt) {
                    fprintf(stderr, "Error: option '-%c' requires an argument\n\n", optopt);
                } else {
                    fprintf(stderr, "Error: unrecognized option '%s'\n\n", argv[optind - 1]);
                }
                usage(stderr, argv[0]);
                return -1;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "Error: unexpected argument '%s'\n\n", argv[optind]);
        usage(stderr, argv[0]);
        return -1;
    }

    if (out->t_src == 0x2) {
        fprintf(stderr, "Error: missing required option -s/--source\n\n");
        usage(stderr, argv[0]);
        return -1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    // Lock memory
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        perror("mlockall failed");
    }
    thread_setup(pthread_self(),2);
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    setenv("TZ", "Europe/Berlin", 1);
    tzset();

    
    parser_t cli_vars;
    if (parse_arguments(argc, argv, &cli_vars) != 0) {
        return 1;
    }
    cli_vars.stop_thread = &stop_thread;
    cli_vars.lck = &lck;
    cli_vars.ext = &ext;
    cli_vars.exit = &thread_exit;
    
    if (cli_vars.verbose) printf("%s v%s\n", DCF77_PROJECT_NAME, DCF77_PROJECT_VERSION);

    //Carrier Initialization thread
    pthread_t carrier_tid;
    pthread_create(&carrier_tid, NULL, carrier_conf, &cli_vars);

    //Attenuator Initialization thread
    pthread_t attenuator_tid;
    pthread_create(&attenuator_tid, NULL, data_tx, &cli_vars);


    pthread_mutex_lock(&lck);
    while (thread_exit == 0 && !atomic_load_explicit(&stop_thread, memory_order_acquire)) {
        pthread_cond_wait(&ext, &lck);
    }
    pthread_mutex_unlock(&lck);

    handle_signal(SIGINT); //Trigger interrupt behavior to shutdown carrier after attenuator finished

    pthread_join(attenuator_tid, NULL);// Await attenuator thread to exit first
    pthread_join(carrier_tid, NULL); //Await carrier thread to exit first
    return 0;
}
