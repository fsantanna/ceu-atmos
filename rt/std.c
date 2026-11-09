// Standard library.

#include <stdio.h>

#include "atmos.h"

AVal atm_g_print;

///////////////////////////////////////////////////////////////////////////////
// BASE
///////////////////////////////////////////////////////////////////////////////

static void print1 (AVal v) {
    AVal s = a_tostring(v);
    fwrite(A_STR_OF(s)->s, 1, A_STR_OF(s)->len, stdout);
    a_dec(s);
}

// print(...): shows the arguments separated by tabs.
static AStatus std_print (AFunc* self, int argc, AVal* argv) {
    (void) self;
    for (int i=0; i<argc; i++) {
        if (i > 0) {
            fputc('\t', stdout);
        }
        print1(argv[i]);
    }
    fputc('\n', stdout);
    return A_OK;
}

///////////////////////////////////////////////////////////////////////////////

void atm_std_init (void) {
    atm_g_print = a_func(std_print, 0);
}

void atm_std_quit (void) {
    a_clr(&atm_g_print);
}
