// Atmos runtime: public C API for generated code, natives, and
// environments.

#ifndef ATMOS_H
#define ATMOS_H

#include <stdint.h>
#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// VALUES
///////////////////////////////////////////////////////////////////////////////

// Types from A_STR on are heap objects with an AObj header.
typedef enum {
    A_NIL, A_BOOL, A_NUM,
    A_STR, A_TAB, A_FUNC, A_TASK, A_XTASK, A_TASKS, A_NAT
} AType;

typedef struct AObj {
    int32_t  rc;
    uint8_t  type;
    uint8_t  pinned;
    uint32_t depth;
} AObj;

typedef struct AVal {
    uint8_t type;
    union {
        int    b;
        double n;
        AObj*  o;
    } u;
} AVal;

static inline AVal a_nil (void) {
    AVal v;
    v.type = A_NIL;
    v.u.o  = NULL;
    return v;
}

static inline AVal a_bool (int b) {
    AVal v;
    v.type = A_BOOL;
    v.u.b  = !!b;
    return v;
}

static inline AVal a_num (double n) {
    AVal v;
    v.type = A_NUM;
    v.u.n  = n;
    return v;
}

static inline AVal a_obj (AObj* o) {
    AVal v;
    v.type = o->type;
    v.u.o  = o;
    return v;
}

static inline int a_truthy (AVal v) {
    return !(v.type==A_NIL || (v.type==A_BOOL && !v.u.b));
}

///////////////////////////////////////////////////////////////////////////////
// STATUS
///////////////////////////////////////////////////////////////////////////////

// Non-local exits propagate as a returned status, never longjmp.
typedef enum {
    A_OK, A_ERROR, A_ESCAPE, A_BREAK, A_RETURN, A_ABORT
} AStatus;

// Raises an error with a formatted message.
AStatus a_error (const char* fmt, ...);

// Message of the pending error.
const char* a_error_msg (void);

///////////////////////////////////////////////////////////////////////////////
// MEMORY
///////////////////////////////////////////////////////////////////////////////

// Ownership convention:
//  - arguments are borrowed
//  - return values are owned (+1)
//  - a variable, slot, or stack position owns one reference

void a_free (AObj* o);

static inline void a_inc (AVal v) {
    if (v.type >= A_STR) {
        v.u.o->rc++;
    }
}

static inline void a_dec (AVal v) {
    if (v.type >= A_STR) {
        if (--v.u.o->rc == 0) {
            a_free(v.u.o);
        }
    }
}

// Stores an owned value into a slot, releasing the old one.
static inline void a_set (AVal* dst, AVal v) {
    AVal old = *dst;
    *dst = v;
    a_dec(old);
}

// Stores a borrowed value into a slot, releasing the old one.
static inline void a_cpy (AVal* dst, AVal v) {
    a_inc(v);
    a_set(dst, v);
}

// Releases a slot.
static inline void a_clr (AVal* dst) {
    a_set(dst, a_nil());
}

// Initializes `n` fresh slots.
static inline void a_nils (AVal* vs, int n) {
    for (int i=0; i<n; i++) {
        vs[i] = a_nil();
    }
}

// Releases `n` slots.
static inline void a_clrs (AVal* vs, int n) {
    for (int i=0; i<n; i++) {
        a_clr(&vs[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////
// VALUE STACK
///////////////////////////////////////////////////////////////////////////////

// One global value stack: the caller pushes arguments, the callee
// pushes results. Every position below `a_top` owns its value.

extern AVal* a_stk;
extern int   a_top;

void a_stk_overflow (void);

#define A_STK_MAX (1 << 18)

// Pushes an owned value.
static inline void a_push (AVal v) {
    if (a_top >= A_STK_MAX) {
        a_stk_overflow();
    }
    a_stk[a_top++] = v;
}

// Pushes a borrowed value.
static inline void a_pushc (AVal v) {
    a_inc(v);
    a_push(v);
}

// Pushes the value of a slot, emptying it.
static inline void a_pushm (AVal* src) {
    a_push(*src);
    *src = a_nil();
}

// Releases all values from `top` up.
static inline void a_settop (int top) {
    while (a_top > top) {
        a_dec(a_stk[--a_top]);
    }
}

// Moves the first result at `base` (or nil) into a slot and releases
// the others.
static inline void a_ret1 (AVal* dst, int base) {
    if (a_top > base) {
        a_set(dst, a_stk[base]);
        a_stk[base] = a_nil();
    } else {
        a_clr(dst);
    }
    a_settop(base);
}

///////////////////////////////////////////////////////////////////////////////
// FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

struct AFunc;

// Arguments are `argv[0..argc)`, on the value stack. The function
// pushes its results and returns a status.
typedef AStatus (*AFn) (struct AFunc* self, int argc, AVal* argv);

typedef struct AFunc {
    AObj hdr;
    AFn  fn;
    int  ncap;
    AVal caps[];
} AFunc;

AVal a_func (AFn fn, int ncap);

// Calls `f` with the arguments at `[base,a_top)`.
// On success, the results replace the arguments at `[base,a_top)`.
// Otherwise, the stack is unwound to `base`.
AStatus a_call (AVal f, int base);

///////////////////////////////////////////////////////////////////////////////
// OPERATIONS
///////////////////////////////////////////////////////////////////////////////

typedef enum {
    A_ADD, A_SUB, A_MUL, A_DIV, A_IDIV, A_MOD, A_POW
} AArith;

// Slow path of arithmetic: computes or raises.
AStatus a_arith (AArith op, AVal* dst, AVal a, AVal b);

#define A_ARITH_FAST(name, OP, expr)                                \
    static inline AStatus name (AVal* dst, AVal a, AVal b) {        \
        if (a.type==A_NUM && b.type==A_NUM) {                       \
            a_set(dst, a_num(expr));                                \
            return A_OK;                                            \
        }                                                           \
        return a_arith(OP, dst, a, b);                              \
    }

A_ARITH_FAST(a_add, A_ADD, a.u.n + b.u.n)
A_ARITH_FAST(a_sub, A_SUB, a.u.n - b.u.n)
A_ARITH_FAST(a_mul, A_MUL, a.u.n * b.u.n)
A_ARITH_FAST(a_div, A_DIV, a.u.n / b.u.n)

static inline AStatus a_idiv (AVal* dst, AVal a, AVal b) {
    return a_arith(A_IDIV, dst, a, b);
}

static inline AStatus a_mod (AVal* dst, AVal a, AVal b) {
    return a_arith(A_MOD, dst, a, b);
}

static inline AStatus a_pow (AVal* dst, AVal a, AVal b) {
    return a_arith(A_POW, dst, a, b);
}

// -a
AStatus a_neg (AVal* dst, AVal a);

// !a
static inline void a_not (AVal* dst, AVal a) {
    a_set(dst, a_bool(!a_truthy(a)));
}

// Name of the type of a value: "nil", "number", ...
const char* a_typename (AVal v);

// Writes a number as `print` shows it: integral values without
// decimals, others with 14 significant digits.
int a_num_fmt (char* buf, size_t n, double v);

///////////////////////////////////////////////////////////////////////////////
// STDLIB
///////////////////////////////////////////////////////////////////////////////

extern AVal atm_g_print;

///////////////////////////////////////////////////////////////////////////////
// DRIVER
///////////////////////////////////////////////////////////////////////////////

void    atm_init (int argc, char** argv);
void    atm_quit (void);

// Generated: runs the main chunk.
AStatus atm_main (void);

#endif
