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
// A_BOX is internal: it never reaches a program as a value.
typedef enum {
    A_NIL, A_BOOL, A_NUM,
    A_STR, A_TAB, A_FUNC, A_TASK, A_XTASK, A_TASKS, A_NAT,
    A_BOX
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

// Raises an error with a formatted message as its value.
AStatus a_error (const char* fmt, ...);

// Raises the payload as an error.
AStatus a_throw (void);

// First value of the pending error, as a string.
const char* a_error_msg (void);

// Source line being executed, set by the generated code. An error
// records it, and so does every call it propagates through.
extern int a_lin;

// Source file of the program, set by the generated code.
extern const char* atm_file;

// Shows the pending error and its trace: the calls it crossed, from
// the outermost one, then the line that raised it.
void a_error_dump (void);

// Payload of the pending exit: the values of a break, escape, return,
// or throw, kept outside the value stack while blocks unwind.
extern struct AVal* a_pay;
extern int          a_pay_n;

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

// Moves a slot into another, emptying the source.
static inline void a_mov (AVal* dst, AVal* src) {
    AVal v = *src;
    *src = a_nil();
    a_set(dst, v);
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

// Moves the top value into a slot.
static inline void a_pop (AVal* dst) {
    a_set(dst, a_stk[--a_top]);
}

// Makes the top `n` values exactly `want`: drops the extra ones, or
// completes with nils.
static inline void a_adj (int n, int want) {
    for (; n>want; n--) {
        a_dec(a_stk[--a_top]);
    }
    for (; n<want; n++) {
        a_push(a_nil());
    }
}

// Shifts the top `n` values up, opening `k` empty positions below.
static inline void a_open (int n, int k) {
    if (a_top + k > A_STK_MAX) {
        a_stk_overflow();
    }
    for (int i=a_top-1; i>=a_top-n; i--) {
        a_stk[i+k] = a_stk[i];
    }
    for (int i=a_top-n; i<a_top-n+k; i++) {
        a_stk[i] = a_nil();
    }
    a_top += k;
}

// Fills an opened position with a borrowed value.
static inline void a_putc (int i, AVal v) {
    a_inc(v);
    a_stk[i] = v;
}

// Fills an opened position with the value of a slot, emptying it.
static inline void a_putm (int i, AVal* src) {
    a_stk[i] = *src;
    *src = a_nil();
}

///////////////////////////////////////////////////////////////////////////////
// PAYLOAD
///////////////////////////////////////////////////////////////////////////////

// Releases the payload.
void a_pay_clear (void);

// Appends an owned value.
void a_pay_add (AVal v);

// Appends a borrowed value.
static inline void a_pay_addc (AVal v) {
    a_inc(v);
    a_pay_add(v);
}

// Appends the value of a slot, emptying it.
static inline void a_pay_addm (AVal* src) {
    a_pay_add(*src);
    *src = a_nil();
}

// Moves value `i` of the payload (or nil) into a slot.
void a_pay_take (AVal* dst, int i);

// Moves the top `n` values of the stack into the payload, replacing
// it.
void a_pay_pull (int n);

// Moves the payload onto the stack. Returns the number of values.
int a_pay_push (void);

// A payload set aside while other code runs (defers, catch tests).
typedef struct APay {
    AVal* v;
    int   n;
    int   cap;
} APay;

// Moves the payload into `s`, leaving it empty.
void a_pay_save (APay* s);

// Releases the payload and moves `s` back.
void a_pay_restore (APay* s);

// Releases a saved payload.
void a_pay_drop (APay* s);

// Tests the pending error against the condition of a catch:
//  - true: any error
//  - false: none
//  - function: called with the thrown values; catches if its first
//    result holds, and the other results replace the thrown values
//  - otherwise: the first thrown value `??` the condition
// Sets `ok`. Fails if the function fails.
AStatus a_catch (AVal cnd, int* ok);

///////////////////////////////////////////////////////////////////////////////
// STRINGS
///////////////////////////////////////////////////////////////////////////////

// Strings are immutable and interned: equal contents share one
// object, so equality is a pointer comparison. Tags are strings.
// Contents are NUL-terminated for C interop.

typedef struct AStr {
    AObj     hdr;
    uint32_t hash;
    uint32_t len;
    char     s[];
} AStr;

#define A_STR_OF(v) ((AStr*)(v).u.o)

// Interned string with the given contents (owned).
AVal a_str  (const char* s, size_t len);
AVal a_strz (const char* s);

///////////////////////////////////////////////////////////////////////////////
// TABLES
///////////////////////////////////////////////////////////////////////////////

// One mutable table type with an array part and a hash part.
//
// Array part: keys `0..an-1`. Its last element is never nil, so `an`
// is always a border: `t@(an-1)` is not nil and `t@(an)` is nil.
//
// Hash part: dense entries in insertion order, plus an open
// addressing index into them. A deleted entry leaves a tombstone,
// compacted on rehash.

typedef struct AEnt {
    AVal k;
    AVal v;
} AEnt;

typedef struct ATab {
    AObj     hdr;
    AVal*    arr;
    uint32_t an;
    uint32_t acap;
    AEnt*    ents;
    uint32_t en;
    uint32_t ecap;
    uint32_t elive;
    int32_t* idx;
    uint32_t icap;
} ATab;

#define A_TAB_OF(v) ((ATab*)(v).u.o)

// New empty table (owned).
AVal a_tab (void);

// Value of a key (borrowed), or nil.
AVal a_tab_get (ATab* t, AVal k);

// Assigns a key, which must be neither nil nor NaN. Nil deletes it.
void a_tab_set (ATab* t, AVal k, AVal v);

// Iteration: array part, then hash part in insertion order.
// Starts with `*pos == 0`. Returns 0 at the end.
int a_tab_next (ATab* t, uint32_t* pos, AVal* k, AVal* v);

// t@k
AStatus a_index (AVal* dst, AVal t, AVal k);

// set t@k = v
AStatus a_setindex (AVal t, AVal k, AVal v);

// #v
AStatus a_len (AVal* dst, AVal v);

// t@#  (last element)
AStatus a_index_last (AVal* dst, AVal t);

// set t@# = v
AStatus a_setindex_last (AVal t, AVal v);

// set t@+ = v  (append)
AStatus a_setindex_next (AVal t, AVal v);

// :X [...]  (sets the `tag` key of a table)
AStatus a_tag (AVal t, AVal tag);

// Moves the top `n` values of the stack into the keys `i, i+1, ...`.
void a_tab_fill (AVal t, int i, int n);

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

#define A_FUNC_OF(v) ((AFunc*)(v).u.o)

// New function with `ncap` empty captures (owned).
AVal a_func (AFn fn, int ncap);

// A captured mutable variable lives in a box, shared by the function
// that declares it and by the closures that capture it.
typedef struct ABox {
    AObj hdr;
    AVal v;
} ABox;

#define A_BOX_OF(x) ((ABox*)(x).u.o)

// New box holding nil (owned).
AVal a_box (void);

// Pushes the extra arguments of a variadic function: those from
// position `npar` on. Returns how many.
static inline int a_dots (int argc, AVal* argv, int npar) {
    for (int i=npar; i<argc; i++) {
        a_pushc(argv[i]);
    }
    return (argc > npar) ? argc-npar : 0;
}

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

// a == b: by value for nil, booleans, and numbers; by reference for
// objects (strings are interned).
static inline int a_eq (AVal a, AVal b) {
    if (a.type != b.type) {
        return 0;
    }
    switch (a.type) {
        case A_NIL:  return 1;
        case A_BOOL: return a.u.b == b.u.b;
        case A_NUM:  return a.u.n == b.u.n;
        default:     return a.u.o == b.u.o;
    }
}

// Slow path of comparisons: strings, or raises.
AStatus a_less (int eq, AVal* dst, AVal a, AVal b);

// a < b
static inline AStatus a_lt (AVal* dst, AVal a, AVal b) {
    if (a.type==A_NUM && b.type==A_NUM) {
        a_set(dst, a_bool(a.u.n < b.u.n));
        return A_OK;
    }
    return a_less(0, dst, a, b);
}

// a <= b
static inline AStatus a_le (AVal* dst, AVal a, AVal b) {
    if (a.type==A_NUM && b.type==A_NUM) {
        a_set(dst, a_bool(a.u.n <= b.u.n));
        return A_OK;
    }
    return a_less(1, dst, a, b);
}

// a ++ b: strings and numbers concatenate; tables merge, with the
// vector part of `b` appended and its other keys favored.
AStatus a_cat (AVal* dst, AVal a, AVal b);

// v ?? x: equal, or `x` names the type of `v`, or `x` is a dot prefix
// of `v` or of its tag.
int a_is (AVal v, AVal x);

// a =>= b: `a` is structurally more general than `b`.
int a_gte (AVal a, AVal b);

// a === b
static inline int a_deq (AVal a, AVal b) {
    return a_gte(a, b) && a_gte(b, a);
}

// v ?> t (or v !> t, if `neg`)
AStatus a_in (AVal* dst, AVal v, AVal t, int neg);

// Advances an iteration, with the cursor in `pos` (initially nil):
//  - table: its pairs, array part first
//  - number n: 0..n-1
//  - number a, with `lim` b: a..b
//  - nil: 0, 1, ...
//  - function: its results, until the first is nil
// Sets `k` to nil at the end.
AStatus a_next (AVal it, AVal lim, AVal* pos, AVal* k, AVal* v);

// String representation, as `print` shows it (owned).
AVal a_tostring (AVal v);

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
extern AVal atm_g_type;
extern AVal atm_g_tostring;
extern AVal atm_g_tonumber;
extern AVal atm_g_assert;
extern AVal atm_g_X;

// Deep string representation: tables as `:Tag [v, k=v]`, with sorted
// keys (owned). Fails on keys that are neither numbers nor strings.
AStatus a_xtostring (AVal* dst, AVal v);

// Deep copy of tables (owned).
AVal a_xcopy (AVal v);

///////////////////////////////////////////////////////////////////////////////
// DRIVER
///////////////////////////////////////////////////////////////////////////////

void    atm_init (int argc, char** argv);
void    atm_quit (void);

// Generated: runs the main chunk, with the program arguments.
AStatus atm_main (int argc, AVal* argv);

#endif
