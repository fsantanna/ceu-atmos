// Values, memory, value stack, calls, and arithmetic.

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atmos.h"

AVal* a_stk = NULL;
int   a_top = 0;

static char ERR[512];

///////////////////////////////////////////////////////////////////////////////
// STATUS
///////////////////////////////////////////////////////////////////////////////

AStatus a_error (const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ERR, sizeof(ERR), fmt, ap);
    va_end(ap);
    return A_ERROR;
}

const char* a_error_msg (void) {
    return ERR;
}

///////////////////////////////////////////////////////////////////////////////
// MEMORY
///////////////////////////////////////////////////////////////////////////////

void a_str_free (AStr* s);
void a_tab_free (ATab* t);

void a_free (AObj* o) {
    switch (o->type) {
        case A_STR:
            a_str_free((AStr*) o);
            break;
        case A_TAB:
            a_tab_free((ATab*) o);
            break;
        case A_FUNC: {
            AFunc* f = (AFunc*) o;
            for (int i=0; i<f->ncap; i++) {
                a_dec(f->caps[i]);
            }
            break;
        }
        default:
            break;
    }
    free(o);
}

void a_stk_overflow (void) {
    fprintf(stderr, "==> ERROR:\n==> stack overflow\n");
    exit(1);
}

///////////////////////////////////////////////////////////////////////////////
// FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

AVal a_func (AFn fn, int ncap) {
    AFunc* f = malloc(sizeof(AFunc) + ncap*sizeof(AVal));
    f->hdr.rc     = 1;
    f->hdr.type   = A_FUNC;
    f->hdr.pinned = 0;
    f->hdr.depth  = 0;
    f->fn   = fn;
    f->ncap = ncap;
    a_nils(f->caps, ncap);
    return a_obj(&f->hdr);
}

AStatus a_call (AVal f, int base) {
    if (f.type != A_FUNC) {
        a_settop(base);
        return a_error("invalid call : expected function : got %s",
            a_typename(f));
    }

    // the function must outlive its own call
    a_inc(f);
    int argc = a_top - base;
    AStatus st = ((AFunc*)f.u.o)->fn((AFunc*)f.u.o, argc, &a_stk[base]);
    a_dec(f);

    if (st != A_OK) {
        a_settop(base);
        return st;
    }

    // slides the results down over the arguments
    int nret = a_top - (base + argc);
    for (int i=0; i<argc; i++) {
        a_dec(a_stk[base+i]);
    }
    for (int i=0; i<nret; i++) {
        a_stk[base+i] = a_stk[base+argc+i];
    }
    a_top = base + nret;
    return A_OK;
}

///////////////////////////////////////////////////////////////////////////////
// OPERATIONS
///////////////////////////////////////////////////////////////////////////////

const char* a_typename (AVal v) {
    switch (v.type) {
        case A_NIL:   return "nil";
        case A_BOOL:  return "boolean";
        case A_NUM:   return "number";
        case A_STR:   return "string";
        case A_TAB:   return "table";
        case A_FUNC:  return "function";
        case A_TASK:  return "task";
        case A_XTASK: return "xtask";
        case A_TASKS: return "tasks";
        case A_NAT:   return "native";
    }
    return "?";
}

static const char* ARITH[] = { "+", "-", "*", "/", "//", "%", "**" };

AStatus a_arith (AArith op, AVal* dst, AVal a, AVal b) {
    if (a.type!=A_NUM || b.type!=A_NUM) {
        AVal x = (a.type != A_NUM) ? a : b;
        return a_error("invalid operation '%s' : expected number : got %s",
            ARITH[op], a_typename(x));
    }
    double x = a.u.n;
    double y = b.u.n;
    double r = 0;
    switch (op) {
        case A_ADD:  r = x + y;         break;
        case A_SUB:  r = x - y;         break;
        case A_MUL:  r = x * y;         break;
        case A_DIV:  r = x / y;         break;
        case A_IDIV: r = floor(x / y);  break;
        case A_POW:  r = pow(x, y);     break;
        case A_MOD:
            // as in Lua: the result has the sign of the divisor
            if (isinf(y)) {
                r = (x==x && ((x>=0) == (y>0))) ? x : y;
            } else {
                r = fmod(x, y);
                if (r!=0 && ((r<0) != (y<0))) {
                    r += y;
                }
            }
            break;
    }
    a_set(dst, a_num(r));
    return A_OK;
}

AStatus a_neg (AVal* dst, AVal a) {
    if (a.type != A_NUM) {
        return a_error("invalid operation '-' : expected number : got %s",
            a_typename(a));
    }
    a_set(dst, a_num(-a.u.n));
    return A_OK;
}

AStatus a_less (int eq, AVal* dst, AVal a, AVal b) {
    if (a.type==A_NUM && b.type==A_NUM) {
        a_set(dst, a_bool(eq ? a.u.n<=b.u.n : a.u.n<b.u.n));
        return A_OK;
    }
    if (a.type==A_STR && b.type==A_STR) {
        // byte order, shorter first on a common prefix
        AStr* x = A_STR_OF(a);
        AStr* y = A_STR_OF(b);
        size_t n = (x->len < y->len) ? x->len : y->len;
        int c = memcmp(x->s, y->s, n);
        if (c == 0) {
            c = (x->len > y->len) - (x->len < y->len);
        }
        a_set(dst, a_bool(eq ? c<=0 : c<0));
        return A_OK;
    }
    return a_error("invalid comparison : %s with %s",
        a_typename(a), a_typename(b));
}

AVal a_tostring (AVal v) {
    char buf[64];
    switch (v.type) {
        case A_NIL:
            return a_strz("nil");
        case A_BOOL:
            return a_strz(v.u.b ? "true" : "false");
        case A_NUM:
            a_num_fmt(buf, sizeof(buf), v.u.n);
            return a_strz(buf);
        case A_STR:
            a_inc(v);
            return v;
        default:
            snprintf(buf, sizeof(buf), "%s: %p",
                a_typename(v), (void*)v.u.o);
            return a_strz(buf);
    }
}

AStatus a_cat (AVal* dst, AVal a, AVal b) {
    int ok_a = (a.type==A_STR || a.type==A_NUM);
    int ok_b = (b.type==A_STR || b.type==A_NUM);
    if (!ok_a || !ok_b) {
        return a_error("invalid operation '++' : unexpected %s",
            a_typename(ok_a ? b : a));
    }
    AVal x = a_tostring(a);
    AVal y = a_tostring(b);
    size_t nx = A_STR_OF(x)->len;
    size_t ny = A_STR_OF(y)->len;
    char* buf = malloc(nx + ny + 1);
    memcpy(buf, A_STR_OF(x)->s, nx);
    memcpy(buf+nx, A_STR_OF(y)->s, ny);
    a_dec(x);
    a_dec(y);
    AVal ret = a_str(buf, nx+ny);
    free(buf);
    a_set(dst, ret);
    return A_OK;
}

int a_num_fmt (char* buf, size_t n, double v) {
    if (v==floor(v) && fabs(v)<1e15) {
        return snprintf(buf, n, "%.0f", v);
    } else {
        return snprintf(buf, n, "%.14g", v);
    }
}

///////////////////////////////////////////////////////////////////////////////
// DRIVER
///////////////////////////////////////////////////////////////////////////////

void atm_std_init (void);
void atm_std_quit (void);
void a_str_quit (void);

void atm_init (int argc, char** argv) {
    (void) argc;
    (void) argv;
    a_stk = malloc(A_STK_MAX * sizeof(AVal));
    a_top = 0;
    atm_std_init();
}

void atm_quit (void) {
    atm_std_quit();
    a_settop(0);
    free(a_stk);
    a_stk = NULL;
    a_str_quit();
}
