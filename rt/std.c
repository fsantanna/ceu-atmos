// Standard library.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atmos.h"
#include "lib.h"

AVal atm_g_print;
AVal atm_g_type;
AVal atm_g_tostring;
AVal atm_g_tonumber;
AVal atm_g_assert;
AVal atm_g_X;
AVal atm_g_atmos;
AVal atm_g_math;

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

// tonumber(v): a number, or a string holding a whole number literal:
// an integer if it has neither a fraction nor an exponent.
static AStatus std_tonumber (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AVal v = arg(argc, argv, 0);
    if (a_isnum(v)) {
        a_push(v);
        return A_OK;
    }
    if (v.type != A_STR) {
        a_push(a_nil());
        return A_OK;
    }
    const char* s = A_STR_OF(v)->s;
    while (*s==' ' || *s=='\t' || *s=='\n') {
        s++;
    }
    const char* d = (*s=='-' || *s=='+') ? s+1 : s;
    int hex = (d[0]=='0' && (d[1]=='x' || d[1]=='X'));
    char* end;
    AVal ret;
    if (strpbrk(s, hex ? ".pP" : ".eEnN") == NULL) {
        uint64_t u = strtoull(d, &end, 0);
        ret = a_int((int64_t)((*s == '-') ? 0 - u : u));
    } else {
        ret = a_num(strtod(s, &end));
    }
    int ok = (end != s) && (end != d);
    while (*end==' ' || *end=='\t' || *end=='\n') {
        end++;
    }
    ok = ok && (size_t)(end - A_STR_OF(v)->s) == A_STR_OF(v)->len;
    a_push(ok ? ret : a_nil());
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
// MATH
///////////////////////////////////////////////////////////////////////////////

// Argument `i` as a number.
static AStatus argn (int argc, AVal* argv, int i, const char* f, double* n) {
    AVal v = arg(argc, argv, i);
    if (!a_isnum(v)) {
        return a_error("invalid argument to '%s' : expected number : got %s",
            f, a_typename(v));
    }
    *n = a_dbl(v);
    return A_OK;
}

#define MATH1(name, expr)                                               \
    static AStatus math_##name (AFunc* self, int argc, AVal* argv) {    \
        (void) self;                                                    \
        double x = 0;                                                   \
        if (argn(argc, argv, 0, #name, &x) != A_OK) {                   \
            return A_ERROR;                                             \
        }                                                               \
        a_push(a_num(expr));                                            \
        return A_OK;                                                    \
    }

MATH1(sqrt,  sqrt(x))
MATH1(sin,   sin(x))
MATH1(cos,   cos(x))

// math.floor(x) / math.ceil(x): an integer, if it fits
static AStatus math_round (int up, int argc, AVal* argv) {
    double x = 0;
    if (argn(argc, argv, 0, up ? "ceil" : "floor", &x) != A_OK) {
        return A_ERROR;
    }
    a_push((argv[0].type == A_INT) ? argv[0] : a_numi(up ? ceil(x) : floor(x)));
    return A_OK;
}

static AStatus math_floor (AFunc* self, int argc, AVal* argv) {
    (void) self;
    return math_round(0, argc, argv);
}

static AStatus math_ceil (AFunc* self, int argc, AVal* argv) {
    (void) self;
    return math_round(1, argc, argv);
}

// math.abs(x): keeps the subtype
static AStatus math_abs (AFunc* self, int argc, AVal* argv) {
    (void) self;
    double x = 0;
    if (argn(argc, argv, 0, "abs", &x) != A_OK) {
        return A_ERROR;
    }
    if (argv[0].type == A_INT) {
        int64_t i = argv[0].u.i;
        a_push(a_int((i < 0) ? (int64_t)(0 - (uint64_t)i) : i));
    } else {
        a_push(a_num(fabs(x)));
    }
    return A_OK;
}

// math.pow(x, y)
static AStatus math_pow (AFunc* self, int argc, AVal* argv) {
    (void) self;
    double x = 0;
    double y = 0;
    if (argn(argc,argv,0,"pow",&x)!=A_OK || argn(argc,argv,1,"pow",&y)!=A_OK) {
        return A_ERROR;
    }
    a_push(a_num(pow(x, y)));
    return A_OK;
}

// math.min(...) / math.max(...)
static AStatus math_minmax (int max, int argc, AVal* argv) {
    const char* f = max ? "max" : "min";
    double x = 0;
    if (argn(argc, argv, 0, f, &x) != A_OK) {
        return A_ERROR;
    }
    // the chosen argument keeps its subtype
    AVal ret = argv[0];
    for (int i=1; i<argc; i++) {
        if (argn(argc, argv, i, f, &x) != A_OK) {
            return A_ERROR;
        }
        int c = a_num_cmp(argv[i], ret);
        if (max ? c==1 : c==-1) {
            ret = argv[i];
        }
    }
    a_push(ret);
    return A_OK;
}

static AStatus math_min (AFunc* self, int argc, AVal* argv) {
    (void) self;
    return math_minmax(0, argc, argv);
}

static AStatus math_max (AFunc* self, int argc, AVal* argv) {
    (void) self;
    return math_minmax(1, argc, argv);
}

// math.random(): [0,1)
// math.random(n): 0..n-1
// math.random(a, b): a..b
static AStatus math_random (AFunc* self, int argc, AVal* argv) {
    (void) self;
    double r = rand() / (RAND_MAX + 1.0);
    if (argc == 0) {
        a_push(a_num(r));
        return A_OK;
    }
    double a = 0;
    double b = 0;
    if (argn(argc, argv, 0, "random", &b) != A_OK) {
        return A_ERROR;
    }
    if (argc > 1) {
        a = b;
        if (argn(argc, argv, 1, "random", &b) != A_OK) {
            return A_ERROR;
        }
        b = b + 1;
    }
    if (b <= a) {
        return a_error("invalid argument to 'random' : empty interval");
    }
    a_push(a_numi(a + floor(r * (b - a))));
    return A_OK;
}

// math.randomseed(n)
static AStatus math_randomseed (AFunc* self, int argc, AVal* argv) {
    (void) self;
    double n = 0;
    if (argn(argc, argv, 0, "randomseed", &n) != A_OK) {
        return A_ERROR;
    }
    srand((unsigned) n);
    return A_OK;
}

///////////////////////////////////////////////////////////////////////////////
// X
///////////////////////////////////////////////////////////////////////////////

// Number of a key, if it is a number or a numeric string.
static int knum (AVal k, double* n) {
    if (a_isnum(k)) {
        *n = a_dbl(k);
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
        if (!a_isnum(k) && k.type!=A_STR) {
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

AStatus a_xcopy (AVal* dst, AVal v) {
    if (v.type != A_TAB) {
        // the copy is free to go anywhere
        if (a_chk_store(v, 0) != A_OK) {
            return A_ERROR;
        }
        a_cpy(dst, v);
        return A_OK;
    }
    AVal ret = a_tab();
    uint32_t pos = 0;
    AVal k, x;
    AStatus st = A_OK;
    while (st==A_OK && a_tab_next(A_TAB_OF(v), &pos, &k, &x)) {
        AVal c = a_nil();
        st = a_xcopy(&c, x);
        if (st == A_OK) {
            st = a_chk_store(k, 0);
        }
        if (st == A_OK) {
            a_tab_set(A_TAB_OF(ret), k, c);
        }
        a_dec(c);
    }
    if (st == A_OK) {
        a_set(dst, ret);
    } else {
        a_dec(ret);
    }
    return st;
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
    AVal ret = a_nil();
    AStatus st = a_xcopy(&ret, arg(argc,argv,0));
    if (st == A_OK) {
        a_push(ret);
    }
    return st;
}

// X.eq(a, b): structural equality
static AStatus x_eq (AFunc* self, int argc, AVal* argv) {
    (void) self;
    a_push(a_bool(a_deq(arg(argc,argv,0), arg(argc,argv,1))));
    return A_OK;
}

// X.neq(a, b): structural inequality
static AStatus x_neq (AFunc* self, int argc, AVal* argv) {
    (void) self;
    a_push(a_bool(!a_deq(arg(argc,argv,0), arg(argc,argv,1))));
    return A_OK;
}

// X.gte(a, b): `a` is structurally more general than `b`
static AStatus x_gte (AFunc* self, int argc, AVal* argv) {
    (void) self;
    a_push(a_bool(a_gte(arg(argc,argv,0), arg(argc,argv,1))));
    return A_OK;
}

// X.xin(v, t): membership
static AStatus x_xin (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AVal ret = a_nil();
    AStatus st = a_in(&ret, arg(argc,argv,0), arg(argc,argv,1));
    if (st == A_OK) {
        a_push(ret);
    }
    return st;
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
    a_push((us < 0) ? a_nil() : a_numi(us));
    return A_OK;
}

///////////////////////////////////////////////////////////////////////////////

AStatus arg_str (int argc, AVal* argv, int i, const char* f, AStr** s) {
    AVal v = arg(argc, argv, i);
    if (v.type != A_STR) {
        return a_error("invalid argument to '%s' : expected string : got %s",
            f, a_typename(v));
    }
    *s = A_STR_OF(v);
    return A_OK;
}

AStatus arg_int (int argc, AVal* argv, int i, const char* f, int opt,
    int64_t def, int64_t* n)
{
    AVal v = arg(argc, argv, i);
    if (opt && v.type==A_NIL) {
        *n = def;
        return A_OK;
    }
    if (!a_toint(v, n)) {
        return a_error("invalid argument to '%s' : expected integer : got %s",
            f, a_isnum(v) ? "real" : a_typename(v));
    }
    return A_OK;
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
    field(atm_g_X, "eq",       x_eq);
    field(atm_g_X, "neq",      x_neq);
    field(atm_g_X, "gte",      x_gte);
    field(atm_g_X, "xin",      x_xin);

    atm_g_math = a_tab();
    field(atm_g_math, "floor",      math_floor);
    field(atm_g_math, "ceil",       math_ceil);
    field(atm_g_math, "abs",        math_abs);
    field(atm_g_math, "sqrt",       math_sqrt);
    field(atm_g_math, "sin",        math_sin);
    field(atm_g_math, "cos",        math_cos);
    field(atm_g_math, "pow",        math_pow);
    field(atm_g_math, "min",        math_min);
    field(atm_g_math, "max",        math_max);
    field(atm_g_math, "random",     math_random);
    field(atm_g_math, "randomseed", math_randomseed);
    konst(atm_g_math, "huge", a_num(INFINITY));
    konst(atm_g_math, "pi",   a_num(3.14159265358979323846));

    atm_g_atmos = a_tab();
    field(atm_g_atmos, "status", atmos_status);
    field(atm_g_atmos, "next",   atmos_next);

    atm_lib_string_init();
    atm_lib_vector_init();
}

void atm_std_quit (void) {
    a_clr(&atm_g_print);
    a_clr(&atm_g_type);
    a_clr(&atm_g_tostring);
    a_clr(&atm_g_tonumber);
    a_clr(&atm_g_assert);
    a_clr(&atm_g_X);
    a_clr(&atm_g_atmos);
    a_clr(&atm_g_math);
    atm_lib_string_quit();
    atm_lib_vector_quit();
}
