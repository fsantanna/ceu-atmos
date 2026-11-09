// Atmos compiler: allocation and error helpers.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atmc.h"

const char* FILENAME = "?";

void* xalloc (size_t n) {
    void* p = calloc(1, n ? n : 1);
    if (p == NULL) {
        fprintf(stderr, "atmos : out of memory\n");
        exit(1);
    }
    return p;
}

char* xstrndup (const char* s, size_t n) {
    char* p = xalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char* xfmt (const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char* p = xalloc(n + 1);
    va_start(ap, fmt);
    vsnprintf(p, n + 1, fmt, ap);
    va_end(ap);
    return p;
}

void err (Tk tk, const char* fmt, ...) {
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "%s : line %d : near '%s' : ", FILENAME, tk.lin, tk.str);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}
