// Helpers shared by the library files.

#ifndef ATMOS_LIB_H
#define ATMOS_LIB_H

#include <stdlib.h>
#include <string.h>

#include "atmos.h"

// Growing byte buffer.
typedef struct Buf {
    char*  s;
    size_t n;
    size_t cap;
} Buf;

static inline void buf_add (Buf* b, const char* s, size_t n) {
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2;
        b->s = realloc(b->s, b->cap);
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
}

static inline void buf_str (Buf* b, AVal s) {
    buf_add(b, A_STR_OF(s)->s, A_STR_OF(s)->len);
}

// The buffer as a string (owned), releasing the buffer.
static inline AVal buf_val (Buf* b) {
    AVal v = a_str(b->s ? b->s : "", b->n);
    free(b->s);
    b->s = NULL;
    b->n = b->cap = 0;
    return v;
}

// Argument `i`, or nil.
static inline AVal arg (int argc, AVal* argv, int i) {
    return (i < argc) ? argv[i] : a_nil();
}

// Field `name` of the library table `t`: a function, or a constant.
static inline void field (AVal t, const char* name, AFn fn) {
    AVal k = a_strz(name);
    AVal f = a_func(fn, 0);
    a_tab_set(A_TAB_OF(t), k, f);
    a_dec(k);
    a_dec(f);
}

static inline void konst (AVal t, const char* name, AVal v) {
    AVal k = a_strz(name);
    a_tab_set(A_TAB_OF(t), k, v);
    a_dec(k);
}

// Argument `i` of function `f` as a string, or as an integer. A
// missing integer takes `def`, if `opt`.
AStatus arg_str (int argc, AVal* argv, int i, const char* f, AStr** s);
AStatus arg_int (int argc, AVal* argv, int i, const char* f, int opt,
    int64_t def, int64_t* n);

// Libraries: each creates and releases its global table.
void atm_lib_string_init (void);
void atm_lib_string_quit (void);
void atm_lib_vector_init (void);
void atm_lib_vector_quit (void);
void atm_lib_os_init (void);
void atm_lib_os_quit (void);

#endif
