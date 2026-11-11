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
int a_depth = 0;

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
    // an error unwinds the pin blocks before it is caught
    for (int i=0; i<a_pay_n; i++) {
        if (a_pay[i].type>=A_TAB && a_pdepth(a_pay[i])>0) {
            a_pay_clear();
            a_pay_add(a_strz("invalid escape : pinned value"));
            break;
        }
    }
    return A_ERROR;
}

AStatus a_err_pinned (void) {
    return a_error("invalid escape : pinned value");
}

AStatus a_escape (void) {
    TRACE_THROW = a_lin;
    TRACE_N = 0;
    TRACE_MORE = 0;
    return A_ESCAPE;
}

int a_escape_is (AVal tag) {
    return a_is((a_pay_n > 0) ? a_pay[0] : a_nil(), tag);
}

void a_escape_vals (void) {
    if (a_pay_n > 1) {
        a_dec(a_pay[0]);
        for (int i=1; i<a_pay_n; i++) {
            a_pay[i-1] = a_pay[i];
        }
        a_pay_n--;
    }
}

AStatus a_escape_err (void) {
    // keeps the line of the escape
    a_pay_add(a_nil());
    for (int i=a_pay_n-1; i>0; i--) {
        a_pay[i] = a_pay[i-1];
    }
    a_pay[0] = a_strz("invalid escape : no matching block");
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
        fprintf(stderr, " |  %s:%d (call)\n",
            atm_files[A_LIN_FILE(TRACE_CALLS[i])], A_LIN_LINE(TRACE_CALLS[i]));
    }
    fprintf(stderr, " v  %s:%d (throw)\n",
        atm_files[A_LIN_FILE(TRACE_THROW)], A_LIN_LINE(TRACE_THROW));
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

void a_pay_save (APay* s) {
    s->v   = a_pay;
    s->n   = a_pay_n;
    s->cap = PAY_CAP;
    a_pay   = NULL;
    a_pay_n = 0;
    PAY_CAP = 0;
}

void a_pay_restore (APay* s) {
    a_pay_clear();
    free(a_pay);
    a_pay   = s->v;
    a_pay_n = s->n;
    PAY_CAP = s->cap;
}

void a_pay_drop (APay* s) {
    for (int i=0; i<s->n; i++) {
        a_dec(s->v[i]);
    }
    free(s->v);
}

AStatus a_catch (AVal cnd, int* ok) {
    if (cnd.type == A_BOOL) {
        *ok = cnd.u.b;
        return A_OK;
    }
    if (cnd.type != A_FUNC) {
        *ok = a_is((a_pay_n > 0) ? a_pay[0] : a_nil(), cnd);
        return A_OK;
    }

    // the function may use the payload itself
    int base = a_top;
    for (int i=0; i<a_pay_n; i++) {
        a_pushc(a_pay[i]);
    }
    APay saved;
    a_pay_save(&saved);
    AStatus st = a_call(cnd, base);
    if (st != A_OK) {
        a_pay_drop(&saved);
        return st;
    }
    *ok = (a_top > base) && a_truthy(a_stk[base]);
    if (*ok) {
        a_pay_drop(&saved);
        a_pay_pull(a_top - base - 1);
        a_settop(base);
    } else {
        a_settop(base);
        a_pay_restore(&saved);
    }
    return A_OK;
}

///////////////////////////////////////////////////////////////////////////////
// MEMORY
///////////////////////////////////////////////////////////////////////////////

void a_str_free (AStr* s);
void a_tab_free (ATab* t);
void a_node_free (ANode* n);

void a_free (AObj* o) {
    switch (o->type) {
        case A_STR:
            a_str_free((AStr*) o);
            break;
        case A_TAB:
            a_tab_free((ATab*) o);
            break;
        case A_XTASK:
        case A_TASKS:
            a_node_free((ANode*) o);
            break;
        case A_TASK:
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
        case A_NAT:
            a_nat_close(a_obj(o));
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
    f->task = NULL;
    f->ncap = ncap;
    a_nils(f->caps, ncap);
    return a_obj(&f->hdr);
}

AVal a_task (const AProto* p, int ncap) {
    AVal v = a_func(NULL, ncap);
    v.type = A_TASK;
    v.u.o->type = A_TASK;
    A_FUNC_OF(v)->task = p;
    return v;
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

AVal a_nat (void* ptr, void (*fin)(void*), const char* kind) {
    ANat* n = malloc(sizeof(ANat));
    n->hdr.rc     = 1;
    n->hdr.type   = A_NAT;
    n->hdr.pinned = 0;
    n->hdr.depth  = 0;
    n->ptr  = ptr;
    n->fin  = fin;
    n->kind = kind;
    return a_obj(&n->hdr);
}

void a_nat_close (AVal v) {
    ANat* n = A_NAT_OF(v);
    if (n->ptr!=NULL && n->fin!=NULL) {
        n->fin(n->ptr);
    }
    n->ptr = NULL;
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
    int argc  = a_top - base;
    int lin   = a_lin;
    int depth = a_depth;
    CALLS++;
    AStatus st = ((AFunc*)f.u.o)->fn((AFunc*)f.u.o, argc, &a_stk[base]);
    CALLS--;
    a_lin   = lin;
    a_depth = depth;
    a_dec(f);

    // the call aborted the running task: it unwinds
    st = a_chk_aborted(st);

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
        case A_INT:   return "number";
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

static const char* ARITH[] = { "+", "-", "*", "/", "//", "%" };

AVal a_numi (double n) {
    if (n==floor(n) && n>=-9223372036854775808.0 && n<9223372036854775808.0) {
        return a_int((int64_t) n);
    }
    return a_num(n);
}

int a_toint (AVal v, int64_t* i) {
    if (v.type == A_INT) {
        *i = v.u.i;
        return 1;
    }
    if (v.type == A_NUM) {
        AVal x = a_numi(v.u.n);
        if (x.type == A_INT) {
            *i = x.u.i;
            return 1;
        }
    }
    return 0;
}

// An integer against a real, with no loss of precision.
static int cmp_id (int64_t i, double d) {
    if (d != d) {
        return 2;
    }
    if (d >= 9223372036854775808.0) {
        return -1;
    }
    if (d < -9223372036854775808.0) {
        return 1;
    }
    int64_t t = (int64_t) d;
    if (i != t) {
        return (i < t) ? -1 : 1;
    }
    double frac = d - (double)t;
    return (frac > 0) ? -1 : (frac < 0) ? 1 : 0;
}

int a_num_cmp (AVal a, AVal b) {
    if (a.type==A_INT && b.type==A_INT) {
        return (a.u.i < b.u.i) ? -1 : (a.u.i > b.u.i);
    }
    if (a.type==A_NUM && b.type==A_NUM) {
        if (a.u.n!=a.u.n || b.u.n!=b.u.n) {
            return 2;
        }
        return (a.u.n < b.u.n) ? -1 : (a.u.n > b.u.n);
    }
    if (a.type == A_INT) {
        return cmp_id(a.u.i, b.u.n);
    }
    int c = cmp_id(b.u.i, a.u.n);
    return (c == 2) ? 2 : -c;
}

// Integer arithmetic wraps on overflow.
static AStatus arith_int (AArith op, AVal* dst, int64_t x, int64_t y) {
    uint64_t ux = (uint64_t) x;
    uint64_t uy = (uint64_t) y;
    int64_t r = 0;
    switch (op) {
        case A_ADD: r = (int64_t)(ux + uy); break;
        case A_SUB: r = (int64_t)(ux - uy); break;
        case A_MUL: r = (int64_t)(ux * uy); break;
        case A_IDIV:
        case A_MOD:
            if (y == 0) {
                return a_error("invalid operation '%s' : division by zero",
                    ARITH[op]);
            }
            // the only overflow: the minimum integer by -1
            if (y == -1) {
                r = (op == A_IDIV) ? (int64_t)(0 - ux) : 0;
            } else {
                r = (op == A_IDIV) ? x / y : x % y;
            }
            break;
        default:
            break;
    }
    a_set(dst, a_int(r));
    return A_OK;
}

AStatus a_arith (AArith op, AVal* dst, AVal a, AVal b) {
    if (!a_isnum(a) || !a_isnum(b)) {
        AVal x = !a_isnum(a) ? a : b;
        return a_error("invalid operation '%s' : expected number : got %s",
            ARITH[op], a_typename(x));
    }
    if (a.type==A_INT && b.type==A_INT && op!=A_DIV) {
        return arith_int(op, dst, a.u.i, b.u.i);
    }
    double x = a_dbl(a);
    double y = a_dbl(b);
    double r = 0;
    switch (op) {
        case A_ADD:  r = x + y;         break;
        case A_SUB:  r = x - y;         break;
        case A_MUL:  r = x * y;         break;
        case A_DIV:  r = x / y;         break;
        // as in C: truncation, and the sign of the dividend
        case A_IDIV: r = trunc(x / y);  break;
        case A_MOD:  r = fmod(x, y);    break;
    }
    a_set(dst, a_num(r));
    return A_OK;
}

AStatus a_neg (AVal* dst, AVal a) {
    if (a.type == A_INT) {
        a_set(dst, a_int((int64_t)(0 - (uint64_t)a.u.i)));
        return A_OK;
    }
    if (a.type != A_NUM) {
        return a_error("invalid operation '-' : expected number : got %s",
            a_typename(a));
    }
    a_set(dst, a_num(-a.u.n));
    return A_OK;
}

static const char* BITS[] = { "&", "|", "^", "<<", ">>" };

// Operand of a bitwise operation as an integer.
static AStatus bit_int (const char* op, AVal v, int64_t* i) {
    if (!a_isnum(v)) {
        return a_error("invalid operation '%s' : expected number : got %s",
            op, a_typename(v));
    }
    if (!a_toint(v, i)) {
        return a_error("invalid operation '%s' : expected integer", op);
    }
    return A_OK;
}

static int64_t shl (int64_t x, int64_t n);

// Arithmetic shift to the right.
static int64_t shr (int64_t x, int64_t n) {
    if (n < 0) {
        return (n == INT64_MIN) ? 0 : shl(x, -n);
    }
    if (n >= 64) {
        return (x < 0) ? -1 : 0;
    }
    return (x < 0) ? ~(~x >> n) : x >> n;
}

static int64_t shl (int64_t x, int64_t n) {
    if (n < 0) {
        return (n == INT64_MIN) ? ((x < 0) ? -1 : 0) : shr(x, -n);
    }
    if (n >= 64) {
        return 0;
    }
    return (int64_t)((uint64_t)x << n);
}

AStatus a_bit (ABit op, AVal* dst, AVal a, AVal b) {
    int64_t x = 0;
    int64_t y = 0;
    if (bit_int(BITS[op],a,&x)!=A_OK || bit_int(BITS[op],b,&y)!=A_OK) {
        return A_ERROR;
    }
    int64_t r = 0;
    switch (op) {
        case A_BAND: r = x & y;     break;
        case A_BOR:  r = x | y;     break;
        case A_BXOR: r = x ^ y;     break;
        case A_SHL:  r = shl(x, y); break;
        case A_SHR:  r = shr(x, y); break;
    }
    a_set(dst, a_int(r));
    return A_OK;
}

AStatus a_bnot (AVal* dst, AVal a) {
    int64_t x = 0;
    if (bit_int("~", a, &x) != A_OK) {
        return A_ERROR;
    }
    a_set(dst, a_int(~x));
    return A_OK;
}

AStatus a_less (int eq, AVal* dst, AVal a, AVal b) {
    if (a_isnum(a) && a_isnum(b)) {
        // any comparison with NaN is false
        int c = a_num_cmp(a, b);
        a_set(dst, a_bool(c!=2 && (eq ? c<=0 : c<0)));
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
        case A_INT:
        case A_NUM:
            a_num_fmt(buf, sizeof(buf), v);
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

int a_num_fmt (char* buf, size_t n, AVal v) {
    if (v.type == A_INT) {
        return snprintf(buf, n, "%lld", (long long)v.u.i);
    }
    int len = snprintf(buf, n, "%.14g", v.u.n);
    // a real always looks like a real
    if (strspn(buf, "-0123456789") == (size_t)len) {
        len += snprintf(buf+len, n-len, ".0");
    }
    return len;
}

///////////////////////////////////////////////////////////////////////////////
// DRIVER
///////////////////////////////////////////////////////////////////////////////

void atm_std_init (void);
void atm_std_quit (void);
void a_str_quit (void);
void a_task_init (int argc, char** argv);
void a_task_quit (void);

void atm_init (int argc, char** argv) {
    a_stk = malloc(A_STK_MAX * sizeof(AVal));
    a_top = 0;
    atm_std_init();
    a_task_init(argc, argv);
}

void atm_quit (void) {
    a_task_quit();
    atm_free();
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
