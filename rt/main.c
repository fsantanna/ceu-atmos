// Default driver: runs the main chunk, then lets time pass until the
// program terminates or nothing can awake it.

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <time.h>

#include "atmos.h"

static double now_us (void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec*1e6 + ts.tv_nsec/1e3;
}

int main (int argc, char** argv) {
    atm_init(argc, argv);
    AStatus st = atm_main();

    // sleeps until the nearest timer and delivers the time elapsed
    while (st==A_OK && atm_alive()) {
        double us = atm_next();
        if (us < 0) {
            break;
        }
        fflush(stdout);
        double t0 = now_us();
        struct timespec ts;
        ts.tv_sec  = (time_t) (us / 1e6);
        ts.tv_nsec = (long) ((us - ts.tv_sec*1e6) * 1e3);
        nanosleep(&ts, NULL);
        double dt = now_us() - t0;
        st = atm_emit(a_num((dt > us) ? dt : us));
    }

    // nothing can awake the main chunk: it is aborted
    if (st == A_OK) {
        st = atm_stop();
    }

    fflush(stdout);
    if (st == A_ESCAPE) {
        st = a_escape_err();
    }
    if (st != A_OK) {
        a_error_dump();
    }
    atm_quit();
    return (st == A_OK) ? 0 : 1;
}
