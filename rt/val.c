// Values, memory, value stack, calls, and arithmetic.

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atmos.h"

AVal* a_stk = NULL;
int   a_top = 0;

AVal* a_pay   = NULL;
int   a_pay_n = 0;

static int  PAY_CAP = 0;
static char ERR[512];

int a_lin = 0;

// Trace of the pending error: the line that raised it, and the lines
// of the calls it has crossed (innermost first).
#define TRACE_MAX 32
static int TRACE_THROW = 0;
static int TRACE_CALLS[TRACE_MAX];
static int TRACE_N = 0;
static int TRACE_MORE = 0;

///////////////////////////////////////////////////////////////////////////////
// STATUS
///////////////////////////////////////////////////////////////////////////////

AStatus a_throw (void) {
    TRACE_THROW = a_lin;
    TRACE_N = 0;
    TRACE_MORE = 0;
    return A_ERROR;
}

AStatus a_error (const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ERR, sizeof(ERR), fmt, ap);
    va_end(ap);
    a_pay_clear();
    a_pay_add(a_strz(ERR));
    return a_throw();
}

const char* a_error_msg (void) {
    if (a_pay_n == 0) {
        return "";
    }
    AVal s = a_tostring(a_pay[0]);
    snprintf(ERR, sizeof(ERR), "%s", A_STR_OF(s)->s);
    a_dec(s);
    return ERR;
}

void a_error_dump (void) {
    fflush(stdout);
    fprintf(stderr, "==> ERROR:\n");
    if (TRACE_MORE) {
        fprintf(stderr, " |  ...\n");
    }
    for (int i=TRACE_N-1; i>=0; i--) {
        fprintf(stderr, " |  %s:%d (call)\n", atm_file, TRACE_CALLS[i]);
    }
    fprintf(stderr, " v  %s:%d (throw)\n", atm_file, TRACE_THROW);
    fprintf(stderr, "==> ");
    for (int i=0; i<a_pay_n; i++) {
        AVal s = a_tostring(a_pay[i]);
        fprintf(stderr, "%s%s", (i > 0) ? ", " : "", A_STR_OF(s)->s);
        a_dec(s);
    }
    fprintf(stderr, "\n");
}

///////////////////////////////////////////////////////////////////////////////
// PAYLOAD
///////////////////////////////////////////////////////////////////////////////

void a_pay_clear (void) {
    while (a_pay_n > 0) {
        a_dec(a_pay[--a_pay_n]);
    }
}

void a_pay_add (AVal v) {
    if (a_pay_n == PAY_CAP) {
        PAY_CAP = (PAY_CAP == 0) ? 8 : PAY_CAP*2;
        a_pay = realloc(a_pay, PAY_CAP * sizeof(AVal));
    }
    a_pay[a_pay_n++] = v;
}

void a_pay_take (AVal* dst, int i) {
    if (i < a_pay_n) {
        a_set(dst, a_pay[i]);
        a_pay[i] = a_nil();
    } else {
        a_clr(dst);
    }
}

void a_pay_pull (int n) {
    a_pay_clear();
    for (int i=a_top-n; i<a_top; i++) {
        a_pay_add(a_stk[i]);
    }
    a_top -= n;
}

int a_pay_push (void) {
    int n = a_pay_n;
    for (int i=0; i<n; i++) {
        a_push(a_pay[i]);
    }
    a_pay_n = 0;
    return n;
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
        case A_BOX:
            a_dec(((ABox*)o)->v);
            break;
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

AVal a_box (void) {
    ABox* b = malloc(sizeof(ABox));
    b->hdr.rc     = 1;
    b->hdr.type   = A_BOX;
    b->hdr.pinned = 0;
    b->hdr.depth  = 0;
    b->v = a_nil();
    return a_obj(&b->hdr);
}

// Nested calls in progress: bounded to protect the C stack.
#define A_CALLS_MAX 5000
static int CALLS = 0;

AStatus a_call (AVal f, int base) {
    if (f.type != A_FUNC) {
        a_settop(base);
        return a_error("invalid call : expected function : got %s",
            a_typename(f));
    }
    if (CALLS >= A_CALLS_MAX) {
        a_settop(base);
        return a_error("stack overflow");
    }

    // the function must outlive its own call
    a_inc(f);
    int argc = a_top - base;
    int lin  = a_lin;
    CALLS++;
    AStatus st = ((AFunc*)f.u.o)->fn((AFunc*)f.u.o, argc, &a_stk[base]);
    CALLS--;
    a_lin = lin;
    a_dec(f);

    if (st != A_OK) {
        if (st == A_ERROR) {
            if (TRACE_N < TRACE_MAX) {
                TRACE_CALLS[TRACE_N++] = lin;
            } else {
                TRACE_MORE = 1;
            }
        }
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
        case A_BOX:   return "box";
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
    a_pay_clear();
    free(a_pay);
    a_pay = NULL;
    PAY_CAP = 0;
    a_str_quit();
}
