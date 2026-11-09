// Default driver: runs the main chunk.

#include <stdio.h>

#include "atmos.h"

int main (int argc, char** argv) {
    atm_init(argc, argv);

    // the program arguments are the `...` of the main chunk
    int base = a_top;
    for (int i=1; i<argc; i++) {
        a_push(a_strz(argv[i]));
    }
    AStatus st = atm_main(a_top-base, &a_stk[base]);
    a_settop(base);
    fflush(stdout);
    if (st != A_OK) {
        a_error_dump();
    }
    atm_quit();
    return (st == A_OK) ? 0 : 1;
}
