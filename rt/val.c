// Values, memory, value stack, calls, and arithmetic.

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

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

void a_free (AObj* o) {
    switch (o->type) {
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
}
