// Library `os`.

#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "lib.h"

AVal atm_g_os;

// os.time(): seconds since the epoch.
static AStatus os_time (AFunc* self, int argc, AVal* argv) {
    (void) self;
    (void) argc;
    (void) argv;
    a_push(a_int((int64_t)time(NULL)));
    return A_OK;
}

// os.clock(): seconds of CPU time used by the program.
static AStatus os_clock (AFunc* self, int argc, AVal* argv) {
    (void) self;
    (void) argc;
    (void) argv;
    a_push(a_num((double)clock() / CLOCKS_PER_SEC));
    return A_OK;
}

// os.getenv(name): the value of an environment variable, or nil.
static AStatus os_getenv (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AStr* s;
    if (arg_str(argc, argv, 0, "getenv", &s) != A_OK) {
        return A_ERROR;
    }
    const char* v = getenv(s->s);
    a_push((v == NULL) ? a_nil() : a_strz(v));
    return A_OK;
}

// os.exit(code): ends the program at once, with no defers.
static AStatus os_exit (AFunc* self, int argc, AVal* argv) {
    (void) self;
    int64_t code;
    if (arg_int(argc, argv, 0, "exit", 1, 0, &code) != A_OK) {
        return A_ERROR;
    }
    fflush(NULL);
    _exit((int)code);
    return A_OK;
}

void atm_lib_os_init (void) {
    atm_g_os = a_tab();
    field(atm_g_os, "time",   os_time);
    field(atm_g_os, "clock",  os_clock);
    field(atm_g_os, "getenv", os_getenv);
    field(atm_g_os, "exit",   os_exit);
}

void atm_lib_os_quit (void) {
    a_clr(&atm_g_os);
}
