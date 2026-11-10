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
// A_INT and A_NUM are the two subtypes of `number`: 64-bit integers
// and doubles.
typedef enum {
    A_NIL, A_BOOL, A_INT, A_NUM,
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
        int     b;
        int64_t i;
        double  n;
        AObj*   o;
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

static inline AVal a_int (int64_t i) {
    AVal v;
    v.type = A_INT;
    v.u.i  = i;
    return v;
}

static inline AVal a_num (double n) {
    AVal v;
    v.type = A_NUM;
    v.u.n  = n;
    return v;
}

static inline int a_isnum (AVal v) {
    return v.type==A_INT || v.type==A_NUM;
}

// A number as a double.
static inline double a_dbl (AVal v) {
    return (v.type == A_INT) ? (double)v.u.i : v.u.n;
}

// An integer if the value is integral and fits, else a real.
AVal a_numi (double n);

// A number as an integer, if it has an integral value that fits.
// Returns 0 otherwise.
int a_toint (AVal v, int64_t* i);

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
// A_AWAIT is internal: a task suspended at an await.
// A_JOIN is internal: a branch of a `par :any` terminated.
typedef enum {
    A_OK, A_ERROR, A_ESCAPE, A_BREAK, A_RETURN, A_ABORT, A_AWAIT, A_JOIN
} AStatus;

// Raises an error with a formatted message as its value.
AStatus a_error (const char* fmt, ...);

// Raises the payload as an error.
AStatus a_throw (void);

// Raises the payload as an escape: a tag (or tagged table) to match a
// `do :X` block, then the values of the block.
AStatus a_escape (void);

// The pending escape matches the tag of a block.
int a_escape_is (AVal tag);

// Leaves only the values of the escape in the payload: all but the
// tag, or the tag itself if it came alone.
void a_escape_vals (void);

// Turns an escape that no block matched into an error.
AStatus a_escape_err (void);

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

// Pushes the values of the array part. Returns how many.
int a_tab_unpack (AVal t);

///////////////////////////////////////////////////////////////////////////////
// FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

struct AFunc;

// Arguments are `argv[0..argc)`, on the value stack. The function
// pushes its results and returns a status.
typedef AStatus (*AFn) (struct AFunc* self, int argc, AVal* argv);

struct AProto;

// A function, or a task prototype (type A_TASK, with `task` set).
typedef struct AFunc {
    AObj hdr;
    AFn  fn;
    const struct AProto* task;
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
// TASKS
///////////////////////////////////////////////////////////////////////////////

struct ATask;
typedef struct APatt APatt;

// Why a task is resumed:
//  - R_START: first execution
//  - R_EVENT: an emit matched its await; the results are the payload
//  - R_EXIT:  a child delivered a non-local exit, raised at its await
//  - R_ABORT: it unwinds from its await
typedef enum {
    R_START, R_EVENT, R_EXIT, R_ABORT
} AKind;

// Generated: runs a task from its resumption point until it awaits
// (A_AWAIT) or terminates (any other status).
typedef AStatus (*AResume) (struct ATask* t, AKind k);

// Static description of a task prototype.
typedef struct AProto {
    AResume resume;
    // slots of the frame; parameters take the first ones, then a
    // table with the extra arguments, if variadic
    int nslots;
    int npar;
    int dots;
} AProto;

// New task prototype with `ncap` empty captures (owned).
AVal a_task (const AProto* p, int ncap);

// Node of the task tree: a task or a pool. A node owns one reference
// to each child, in spawn order.
typedef struct ANode {
    AObj   hdr;
    struct ANode*  up;
    struct ANode** dns;
    int    ndns;
    int    cdns;
    // traversals in progress: children are only removed at zero
    int    trav;
    uint8_t dirty;
    // terminated task, or released pool
    uint8_t dead;
    // toggled off: emits skip the subtree, unless a filter matches
    uint8_t toggled;
    struct APatt** filt;
    int    nfilt;
    // tag of the events that toggle the node automatically
    AVal   tog;
} ANode;

typedef enum {
    T_RUNNING, T_SUSPENDED, T_DONE
} TStatus;

// Task instance. The frame is freed at termination; the header lives
// while referenced, so `await t` and `t.pub` keep working.
// Frame of a task, shared with its transparent blocks. It is freed
// when the last of them terminates: a block that is aborted while
// running only unwinds when control returns to it.
typedef struct AFrame {
    int  rc;
    int  n;
    AVal L[];
} AFrame;

typedef struct ATask {
    ANode    node;
    const AProto* desc;
    AFunc*   proto;
    AFrame*  frame;
    AVal*    L;
    int      pc;
    uint8_t  status;
    // transparent: no identity, owned by its enclosing task
    uint8_t  tra;
    // abort requested while running: unwinds when control returns
    uint8_t  aborted;
    // exit delivered by a child
    AStatus  exit;
    // emit counter when the current await was established
    uint32_t time;
    struct APatt* awt;
    AVal     pub;
    AVal     ret;
    // pool it was spawned in, for identification only
    struct ATasks* pool;
} ATask;

// Pool of tasks, with an optional limit (-1 for none).
typedef struct ATasks {
    ANode node;
    int   max;
} ATasks;

#define A_TASK_OF(v)  ((ATask*)(v).u.o)
#define A_TASKS_OF(v) ((ATasks*)(v).u.o)

// Innermost running task (NULL outside the program).
extern struct ATask* a_cur;

// spawn T(...): arguments at `[base,a_top)`. The pool is nil for none;
// a full pool gives nil.
AStatus a_spawn (AVal* dst, AVal proto, AVal pool, int base);

// spawn {...}: a transparent task that shares the frame and the
// captures of its owner.
AStatus a_spawn_tra (AVal* dst, struct ATask* owner, const AProto* p);

// Identity of the `par :any` whose branch raised the pending A_JOIN.
extern void* a_join_key;

// tasks(n)
AStatus a_tasks (AVal* dst, AVal max);

// Number of live tasks in a pool.
int a_tasks_len (struct ATasks* ts);

// Next live task of a pool from position `pos`, which advances, or
// NULL at the end.
struct ATask* a_tasks_next (struct ATasks* ts, int* pos);

// Table with the live tasks of a pool (owned).
AVal a_tasks_tab (AVal ts);

// toggle t(on) with P, ...: takes over the `n` filters.
AStatus a_toggle (AVal t, AVal on, APatt** fs, int n);

// toggle on :T with P, ... {...}: events `:T [false]` and `:T [true]`
// toggle the task from now on. Takes over the filters.
void a_toggle_auto (AVal t, AVal tag, APatt** fs, int n);

// abort(t)
AStatus a_abort (AVal v);

// emit @to (evt): `to` is nil for the default target.
AStatus a_emit (AVal to, AVal evt);

// Await patterns are built from the leaves up, then handed to
// `a_await`. Values are borrowed; operands are taken over.

// Value: boolean, tag, table, or task.
APatt* a_pt_val (AVal v);

// T(...): spawns the prototype with the arguments in a table.
APatt* a_pt_spawn (AVal proto, AVal args);

// [p] until (f)  /  [p] while (f): `base` may be NULL.
APatt* a_pt_pred (int until, APatt* base, AVal f);

// :any ts  /  :all ts
APatt* a_pt_pool (int all, AVal ts);

// !p
APatt* a_pt_not (APatt* a);

// p && q  /  p || q
APatt* a_pt_bin (int and, APatt* a, APatt* b);

// Establishes an await, taking over the pattern. Returns A_OK if it
// is satisfied at once (results in the payload), or A_AWAIT if the
// task must suspend.
AStatus a_await (struct ATask* t, APatt* p);

// Status a task continues with after it is resumed at an await.
AStatus a_resumed (struct ATask* t, AKind k);

// The running task with an identity (owned), or nil.
AVal a_me (void);

// Storage of `pub` of the running task.
AStatus a_pub (AVal** ref);

// pin x = v: a task or pool becomes owned by the block.
AStatus a_pin (AVal* slot);

// val x = v: a task or pool must be pinned somewhere.
AStatus a_chk_task (AVal v);

static inline AStatus a_chk (AVal v) {
    if (v.type!=A_XTASK && v.type!=A_TASKS) {
        return A_OK;
    }
    return a_chk_task(v);
}

// End of the block of a pin: aborts the task or pool. Returns the
// status the block continues with.
AStatus a_pin_end (AVal* slot, AStatus st);

// Starts the main chunk as a task, with the program arguments: runs
// it until it suspends or terminates.
AStatus a_main (const AProto* p);

// Microseconds to the nearest timer (a lower bound), or -1 if none.
double a_clock_next (void);

///////////////////////////////////////////////////////////////////////////////
// OPERATIONS
///////////////////////////////////////////////////////////////////////////////

typedef enum {
    A_ADD, A_SUB, A_MUL, A_DIV, A_IDIV, A_MOD
} AArith;

// Slow path of arithmetic: computes or raises.
AStatus a_arith (AArith op, AVal* dst, AVal a, AVal b);

// Integers wrap on overflow; a mix of integer and real is real.
#define A_ARITH_FAST(name, OP, op)                                  \
    static inline AStatus name (AVal* dst, AVal a, AVal b) {        \
        if (a.type==A_INT && b.type==A_INT) {                       \
            uint64_t x = (uint64_t) a.u.i;                          \
            uint64_t y = (uint64_t) b.u.i;                          \
            a_set(dst, a_int((int64_t)(x op y)));                   \
            return A_OK;                                            \
        }                                                           \
        if (a.type==A_NUM && b.type==A_NUM) {                       \
            a_set(dst, a_num(a.u.n op b.u.n));                      \
            return A_OK;                                            \
        }                                                           \
        return a_arith(OP, dst, a, b);                              \
    }

A_ARITH_FAST(a_add, A_ADD, +)
A_ARITH_FAST(a_sub, A_SUB, -)
A_ARITH_FAST(a_mul, A_MUL, *)

// a / b: always real
static inline AStatus a_div (AVal* dst, AVal a, AVal b) {
    return a_arith(A_DIV, dst, a, b);
}

static inline AStatus a_idiv (AVal* dst, AVal a, AVal b) {
    return a_arith(A_IDIV, dst, a, b);
}

static inline AStatus a_mod (AVal* dst, AVal a, AVal b) {
    return a_arith(A_MOD, dst, a, b);
}

// Compares two numbers exactly: -1, 0, 1, or 2 if unordered (NaN).
int a_num_cmp (AVal a, AVal b);

// a == b: by value for nil, booleans, and numbers; by reference for
// objects (strings are interned). `1 == 1.0` holds.
static inline int a_eq (AVal a, AVal b) {
    if (a.type != b.type) {
        return a_isnum(a) && a_isnum(b) && a_num_cmp(a,b)==0;
    }
    switch (a.type) {
        case A_NIL:  return 1;
        case A_BOOL: return a.u.b == b.u.b;
        case A_INT:  return a.u.i == b.u.i;
        case A_NUM:  return a.u.n == b.u.n;
        default:     return a.u.o == b.u.o;
    }
}

// Slow path of comparisons: strings, or raises.
AStatus a_less (int eq, AVal* dst, AVal a, AVal b);

// a < b
static inline AStatus a_lt (AVal* dst, AVal a, AVal b) {
    if (a.type==A_INT && b.type==A_INT) {
        a_set(dst, a_bool(a.u.i < b.u.i));
        return A_OK;
    }
    return a_less(0, dst, a, b);
}

// a <= b
static inline AStatus a_le (AVal* dst, AVal a, AVal b) {
    if (a.type==A_INT && b.type==A_INT) {
        a_set(dst, a_bool(a.u.i <= b.u.i));
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

// X.gte(a, b): `a` is structurally more general than `b`.
//  - equal values
//  - strings: `a` is a dot prefix of `b`
//  - tables: every field of `a` is more general than the same field
//    of `b`
int a_gte (AVal a, AVal b);

// X.eq(a, b): structural equality.
static inline int a_deq (AVal a, AVal b) {
    return a_gte(a, b) && a_gte(b, a);
}

// X.xin(v, t): `v` is a value or a non-numeric key of a table, a task
// of a pool, or a number in a range.
AStatus a_in (AVal* dst, AVal v, AVal t);

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

// Writes a number as `print` shows it: an integer as `5`, a real with
// 14 significant digits and always as a real (`5.0`).
int a_num_fmt (char* buf, size_t n, AVal v);

///////////////////////////////////////////////////////////////////////////////
// STDLIB
///////////////////////////////////////////////////////////////////////////////

extern AVal atm_g_print;
extern AVal atm_g_type;
extern AVal atm_g_tostring;
extern AVal atm_g_tonumber;
extern AVal atm_g_assert;
extern AVal atm_g_X;
extern AVal atm_g_math;

// Deep string representation: tables as `:Tag [v, k=v]`, with sorted
// keys (owned). Fails on keys that are neither numbers nor strings.
AStatus a_xtostring (AVal* dst, AVal v);

// Deep copy of tables (owned).
AVal a_xcopy (AVal v);

///////////////////////////////////////////////////////////////////////////////
// DRIVER
///////////////////////////////////////////////////////////////////////////////

// An environment drives the program from its own loop:
//
//     atm_init(argc, argv);
//     st = atm_main();
//     while (st==A_OK && atm_alive()) {
//         <wait for input or for atm_next() microseconds>
//         st = atm_emit(evt);     // or atm_step(ms)
//     }
//     if (st == A_OK) st = atm_stop();
//     if (st != A_OK) a_error_dump();
//     atm_quit();

void atm_init (int argc, char** argv);
void atm_quit (void);

// Generated: runs the main chunk until it suspends or terminates.
AStatus atm_main (void);

// Generated: releases the constants and the top-level variables.
void atm_free (void);

// External event, with the global target.
AStatus atm_emit (AVal evt);

// Advances the clock, in milliseconds.
AStatus atm_step (uint32_t ms);

// Microseconds to the nearest timer, or -1 if none.
double atm_next (void);

// The main chunk is still pending.
int atm_alive (void);

// Aborts the main chunk.
AStatus atm_stop (void);

extern AVal atm_g_atmos;

#endif
