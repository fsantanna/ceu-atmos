// Standard library.

#include <stdio.h>

#include "atmos.h"

AVal atm_g_print;

///////////////////////////////////////////////////////////////////////////////
// BASE
///////////////////////////////////////////////////////////////////////////////

static void print1 (AVal v) {
    char buf[64];
    switch (v.type) {
        case A_NIL:
            fputs("nil", stdout);
            break;
        case A_BOOL:
            fputs(v.u.b ? "true" : "false", stdout);
            break;
        case A_NUM:
            a_num_fmt(buf, sizeof(buf), v.u.n);
            fputs(buf, stdout);
            break;
        case A_STR:
            fwrite(A_STR_OF(v)->s, 1, A_STR_OF(v)->len, stdout);
            break;
        default:
            printf("%s: %p", a_typename(v), (void*)v.u.o);
            break;
    }
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
