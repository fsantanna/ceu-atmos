// Default driver: runs the main chunk.

#include <stdio.h>

#include "atmos.h"

int main (int argc, char** argv) {
    atm_init(argc, argv);
    AStatus st = atm_main();
    fflush(stdout);
    if (st != A_OK) {
        fprintf(stderr, "==> ERROR:\n==> %s\n", a_error_msg());
    }
    atm_quit();
    return (st == A_OK) ? 0 : 1;
}
