// Standard library.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atmos.h"

AVal atm_g_print;
AVal atm_g_type;
AVal atm_g_tostring;
AVal atm_g_tonumber;
AVal atm_g_assert;
AVal atm_g_X;
AVal atm_g_atmos;

///////////////////////////////////////////////////////////////////////////////
// AUX
///////////////////////////////////////////////////////////////////////////////

// Growing byte buffer.
typedef struct Buf {
    char*  s;
    size_t n;
    size_t cap;
} Buf;

static void buf_add (Buf* b, const char* s, size_t n) {
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2;
        b->s = realloc(b->s, b->cap);
    }
    memcpy(b->s + b->n, s, n);
    b->n += n;
}

static void buf_str (Buf* b, AVal s) {
    buf_add(b, A_STR_OF(s)->s, A_STR_OF(s)->len);
}

static AVal arg (int argc, AVal* argv, int i) {
    return (i < argc) ? argv[i] : a_nil();
}

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

// type(v): the name of the type as a tag.
static AStatus std_type (AFunc* self, int argc, AVal* argv) {
    (void) self;
    a_push(a_strz(a_typename(arg(argc,argv,0))));
    return A_OK;
}

// tostring(v)
static AStatus std_tostring (AFunc* self, int argc, AVal* argv) {
    (void) self;
    a_push(a_tostring(arg(argc,argv,0)));
    return A_OK;
}

// tonumber(v): a number, or a string holding a whole number literal.
static AStatus std_tonumber (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AVal v = arg(argc, argv, 0);
    if (v.type == A_NUM) {
        a_push(v);
    } else if (v.type == A_STR) {
        char* end;
        const char* s = A_STR_OF(v)->s;
        double n = strtod(s, &end);
        while (*end==' ' || *end=='\t' || *end=='\n') {
            end++;
        }
        int ok = (end != s) && (*end == '\0') &&
                 (size_t)(end - s) == A_STR_OF(v)->len;
        a_push(ok ? a_num(n) : a_nil());
    } else {
        a_push(a_nil());
    }
    return A_OK;
}

// assert(v, msg): raises if `v` is falsy, else returns its arguments.
static AStatus std_assert (AFunc* self, int argc, AVal* argv) {
    (void) self;
    if (!a_truthy(arg(argc,argv,0))) {
        AVal msg = arg(argc, argv, 1);
        if (msg.type == A_NIL) {
            return a_error("assertion failed");
        }
        AVal s = a_tostring(msg);
        AStatus st = a_error("%s", A_STR_OF(s)->s);
        a_dec(s);
        return st;
    }
    for (int i=0; i<argc; i++) {
        a_pushc(argv[i]);
    }
    return A_OK;
}

///////////////////////////////////////////////////////////////////////////////
// X
///////////////////////////////////////////////////////////////////////////////

// Number of a key, if it is a number or a numeric string.
static int knum (AVal k, double* n) {
    if (k.type == A_NUM) {
        *n = k.u.n;
        return 1;
    }
    char* end;
    const char* s = A_STR_OF(k)->s;
    *n = strtod(s, &end);
    return (end != s) && (*end == '\0');
}

// Numbers in order, then everything else by its string.
static int kcmp (const void* p1, const void* p2) {
    AVal k1 = ((const AEnt*)p1)->k;
    AVal k2 = ((const AEnt*)p2)->k;
    double n1, n2;
    if (knum(k1,&n1) && knum(k2,&n2)) {
        return (n1 > n2) - (n1 < n2);
    }
    AVal s1 = a_tostring(k1);
    AVal s2 = a_tostring(k2);
    int ret = strcmp(A_STR_OF(s1)->s, A_STR_OF(s2)->s);
    a_dec(s1);
    a_dec(s2);
    return ret;
}

static AStatus xtostring (Buf* b, AVal v) {
    if (v.type != A_TAB) {
        AVal s = a_tostring(v);
        buf_str(b, s);
        a_dec(s);
        return A_OK;
    }

    // collects the entries, except the tag
    ATab* t = A_TAB_OF(v);
    AEnt* es = malloc((t->an + t->en + 1) * sizeof(AEnt));
    AVal tag = a_nil();
    AVal ktag = a_strz("tag");
    int n = 0;
    uint32_t pos = 0;
    AVal k, x;
    AStatus st = A_OK;
    while (a_tab_next(t, &pos, &k, &x)) {
        if (k.type!=A_NUM && k.type!=A_STR) {
            st = a_error("invalid key : expected number or string : got %s",
                a_typename(k));
            break;
        }
        if (a_eq(k, ktag)) {
            tag = x;
        } else {
            es[n].k = k;
            es[n].v = x;
            n++;
        }
    }
    a_dec(ktag);

    if (st == A_OK) {
        qsort(es, n, sizeof(AEnt), kcmp);
        if (tag.type != A_NIL) {
            AVal s = a_tostring(tag);
            buf_add(b, ":", 1);
            buf_str(b, s);
            buf_add(b, " ", 1);
            a_dec(s);
        }
        buf_add(b, "[", 1);

        // keys 0,1,... in sequence are positional
        double i = 0;
        for (int j=0; j<n && st==A_OK; j++) {
            if (j > 0) {
                buf_add(b, ", ", 2);
            }
            double kn;
            if (knum(es[j].k,&kn) && kn==i) {
                i++;
            } else {
                AVal s = a_tostring(es[j].k);
                buf_str(b, s);
                buf_add(b, "=", 1);
                a_dec(s);
            }
            st = xtostring(b, es[j].v);
        }
        buf_add(b, "]", 1);
    }
    free(es);
    return st;
}

AStatus a_xtostring (AVal* dst, AVal v) {
    Buf b = { NULL, 0, 0 };
    AStatus st = xtostring(&b, v);
    if (st == A_OK) {
        a_set(dst, a_str(b.s ? b.s : "", b.n));
    }
    free(b.s);
    return st;
}

AVal a_xcopy (AVal v) {
    if (v.type != A_TAB) {
        a_inc(v);
        return v;
    }
    AVal ret = a_tab();
    uint32_t pos = 0;
    AVal k, x;
    while (a_tab_next(A_TAB_OF(v), &pos, &k, &x)) {
        AVal c = a_xcopy(x);
        a_tab_set(A_TAB_OF(ret), k, c);
        a_dec(c);
    }
    return ret;
}

// X.tostring(v)
static AStatus x_tostring (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AVal s = a_nil();
    AStatus st = a_xtostring(&s, arg(argc,argv,0));
    if (st == A_OK) {
        a_push(s);
    }
    return st;
}

// X.print(...)
static AStatus x_print (AFunc* self, int argc, AVal* argv) {
    (void) self;
    Buf b = { NULL, 0, 0 };
    AStatus st = A_OK;
    for (int i=0; i<argc && st==A_OK; i++) {
        if (i > 0) {
            buf_add(&b, "\t", 1);
        }
        st = xtostring(&b, argv[i]);
    }
    if (st == A_OK) {
        fwrite(b.s, 1, b.n, stdout);
        fputc('\n', stdout);
    }
    free(b.s);
    return st;
}

// X.copy(v)
static AStatus x_copy (AFunc* self, int argc, AVal* argv) {
    (void) self;
    a_push(a_xcopy(arg(argc,argv,0)));
    return A_OK;
}

///////////////////////////////////////////////////////////////////////////////
// ATMOS
///////////////////////////////////////////////////////////////////////////////

// atmos.status(t): :running, :suspended, or :dead.
static AStatus atmos_status (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AVal t = arg(argc, argv, 0);
    if (t.type != A_XTASK) {
        return a_error("invalid status : expected task");
    }
    switch (A_TASK_OF(t)->status) {
        case T_RUNNING:   a_push(a_strz("running"));   break;
        case T_SUSPENDED: a_push(a_strz("suspended")); break;
        default:          a_push(a_strz("dead"));      break;
    }
    return A_OK;
}

// atmos.next(): microseconds to the nearest timer, or nil.
static AStatus atmos_next (AFunc* self, int argc, AVal* argv) {
    (void) self;
    (void) argc;
    (void) argv;
    double us = a_clock_next();
    a_push((us < 0) ? a_nil() : a_num(us));
    return A_OK;
}

///////////////////////////////////////////////////////////////////////////////

static void field (AVal t, const char* name, AFn fn) {
    AVal k = a_strz(name);
    AVal f = a_func(fn, 0);
    a_tab_set(A_TAB_OF(t), k, f);
    a_dec(k);
    a_dec(f);
}

void atm_std_init (void) {
    atm_g_print    = a_func(std_print, 0);
    atm_g_type     = a_func(std_type, 0);
    atm_g_tostring = a_func(std_tostring, 0);
    atm_g_tonumber = a_func(std_tonumber, 0);
    atm_g_assert   = a_func(std_assert, 0);

    atm_g_X = a_tab();
    field(atm_g_X, "tostring", x_tostring);
    field(atm_g_X, "print",    x_print);
    field(atm_g_X, "copy",     x_copy);

    atm_g_atmos = a_tab();
    field(atm_g_atmos, "status", atmos_status);
    field(atm_g_atmos, "next",   atmos_next);
}

void atm_std_quit (void) {
    a_clr(&atm_g_print);
    a_clr(&atm_g_type);
    a_clr(&atm_g_tostring);
    a_clr(&atm_g_tonumber);
    a_clr(&atm_g_assert);
    a_clr(&atm_g_X);
    a_clr(&atm_g_atmos);
}
