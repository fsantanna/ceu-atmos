// Tasks: tree, spawn, resume, emit, await, abort.

#include <stdlib.h>
#include <string.h>

#include "atmos.h"

ATask* a_cur = NULL;

// Root of the task tree.
static ATasks ROOT;

// Emit counter: a task only reacts to emits that start after its
// current await was established.
static uint32_t TIME = 1;

static AStatus node_abort (ANode* n);
static AStatus emit_top   (ANode* n, AVal evt);

///////////////////////////////////////////////////////////////////////////////
// PATTERNS
///////////////////////////////////////////////////////////////////////////////

typedef enum {
    // any event / never
    PK_TRUE, PK_FALSE,
    // event ?? value
    PK_IS,
    // table =>= event
    PK_GTE,
    // termination of a task
    PK_TASK
} PKind;

typedef struct APatt {
    PKind kind;
    AVal  v;
} APatt;

static void patt_free (APatt* p) {
    if (p != NULL) {
        a_dec(p->v);
        free(p);
    }
}

// Classifies a value as a pattern.
static AStatus patt_val (APatt** ret, AVal v) {
    PKind kind;
    switch (v.type) {
        case A_BOOL:
            kind = v.u.b ? PK_TRUE : PK_FALSE;
            break;
        case A_TAB:
            kind = PK_GTE;
            break;
        case A_XTASK:
            kind = PK_TASK;
            break;
        case A_STR:
            kind = PK_IS;
            break;
        case A_NIL:
            return a_error("invalid await : invalid event pattern");
        case A_TASKS:
            return a_error("invalid await : unexpected tasks pool");
        case A_FUNC:
            return a_error("invalid await : unexpected function");
        case A_NUM:
            return a_error("invalid await : clock : not implemented");
        default:
            return a_error("invalid await : unexpected %s", a_typename(v));
    }
    APatt* p = malloc(sizeof(APatt));
    p->kind = kind;
    p->v    = v;
    a_inc(v);
    *ret = p;
    return A_OK;
}

// The pattern is satisfied before any event: results in the payload.
static int patt_start (APatt* p) {
    if (p->kind==PK_TASK && A_TASK_OF(p->v)->status==T_DONE) {
        a_pay_clear();
        a_pay_addc(A_TASK_OF(p->v)->ret);
        a_pay_addc(p->v);
        return 1;
    }
    return 0;
}

// The event satisfies the pattern: results in the payload.
static int patt_match (APatt* p, AVal evt) {
    int ok = 0;
    switch (p->kind) {
        case PK_TRUE:
            ok = 1;
            break;
        case PK_FALSE:
            ok = 0;
            break;
        case PK_IS:
            ok = a_is(evt, p->v);
            break;
        case PK_GTE:
            ok = (evt.type != A_XTASK) && a_gte(p->v, evt);
            break;
        case PK_TASK:
            if (a_eq(evt, p->v)) {
                a_pay_clear();
                a_pay_addc(A_TASK_OF(p->v)->ret);
                a_pay_addc(p->v);
                return 1;
            }
            return 0;
    }
    if (ok) {
        a_pay_clear();
        a_pay_addc(evt);
    }
    return ok;
}

///////////////////////////////////////////////////////////////////////////////
// TREE
///////////////////////////////////////////////////////////////////////////////

static void node_init (ANode* n, AType type) {
    memset(n, 0, sizeof(ANode));
    n->hdr.rc   = 1;
    n->hdr.type = type;
}

// Appends a child, taking over one reference.
static void node_add (ANode* up, ANode* n) {
    if (up->ndns == up->cdns) {
        up->cdns = (up->cdns == 0) ? 4 : up->cdns*2;
        up->dns  = realloc(up->dns, up->cdns * sizeof(ANode*));
    }
    up->dns[up->ndns++] = n;
    n->up = up;
}

// Removes the dead children, releasing them.
static void node_sweep (ANode* n) {
    int k = 0;
    for (int i=0; i<n->ndns; i++) {
        ANode* dn = n->dns[i];
        if (dn->dead) {
            dn->up = NULL;
            a_dec(a_obj(&dn->hdr));
        } else {
            n->dns[k++] = dn;
        }
    }
    n->ndns  = k;
    n->dirty = 0;
}

// A child died: it leaves now, or when the traversals end.
static void node_died (ANode* n) {
    n->dead = 1;
    ANode* up = n->up;
    if (up != NULL) {
        up->dirty = 1;
        if (up->trav == 0) {
            node_sweep(up);
        }
    }
}

static void node_trav (ANode* n, int inc) {
    n->trav += inc;
    if (n->trav==0 && n->dirty) {
        node_sweep(n);
    }
}

void a_node_free (ANode* n) {
    if (n->hdr.type == A_XTASK) {
        ATask* t = (ATask*) n;
        patt_free(t->awt);
        if (t->L != NULL) {
            a_clrs(t->L, t->proto->task->nslots);
            free(t->L);
        }
        if (t->proto != NULL) {
            a_dec(a_obj(&t->proto->hdr));
        }
        a_dec(t->pub);
        a_dec(t->ret);
    }
    free(n->dns);
}

// The enclosing task with an identity (transparent tasks and pools
// are skipped), or NULL.
static ATask* task_of (ANode* n) {
    while (n != NULL) {
        if (n->hdr.type==A_XTASK && !((ATask*)n)->tra) {
            return (ATask*) n;
        }
        n = n->up;
    }
    return NULL;
}

// `n` is the running task or one of its ancestors.
static int is_up (ANode* n) {
    for (ANode* x=(ANode*)a_cur; x!=NULL; x=x->up) {
        if (x == n) {
            return 1;
        }
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// RESUME
///////////////////////////////////////////////////////////////////////////////

// The task terminated with `st`. Returns the status its resumer
// continues with.
static AStatus task_end (ATask* t, AStatus st) {
    t->status = T_DONE;
    patt_free(t->awt);
    t->awt = NULL;

    // its blocks already aborted the children: these are leftovers
    node_trav(&t->node, 1);
    for (int i=0; i<t->node.ndns; i++) {
        node_abort(t->node.dns[i]);
    }
    node_trav(&t->node, -1);

    // the frame goes, the header stays
    a_clrs(t->L, t->proto->task->nslots);
    free(t->L);
    t->L = NULL;
    a_dec(a_obj(&t->proto->hdr));
    t->proto = NULL;

    // owner: the enclosing task with an identity (beyond its pool)
    ANode* up = t->node.up;
    if (up!=NULL && up->hdr.type==A_TASKS) {
        up = up->up;
    }
    ATask* owner = task_of(up);
    node_died(&t->node);

    switch (st) {
        case A_OK:
            // termination is an emit of the task itself to the
            // subtree of its owner
            return emit_top((owner != NULL) ? &owner->node : &ROOT.node,
                a_obj(&t->node.hdr));
        case A_ABORT:
            return A_OK;
        default:
            return st;
    }
}

static AStatus task_resume (ATask* t, AKind k) {
    ATask* prev = a_cur;
    AVal me = a_obj(&t->node.hdr);
    a_inc(me);
    a_cur = t;
    t->status = T_RUNNING;
    int lin = a_lin;
    AStatus st = t->proto->task->resume(t, k);
    a_lin = lin;
    a_cur = prev;
    if (st == A_AWAIT) {
        t->status = T_SUSPENDED;
        st = A_OK;
    } else {
        st = task_end(t, st);
    }
    a_dec(me);
    return st;
}

///////////////////////////////////////////////////////////////////////////////
// ABORT
///////////////////////////////////////////////////////////////////////////////

// Children first, in spawn order, then the task itself: a suspended
// task unwinds now; a running one, when control returns to it.
static AStatus node_abort (ANode* n) {
    if (n->dead) {
        return A_OK;
    }
    AStatus st = A_OK;
    node_trav(n, 1);
    int cnt = n->ndns;
    for (int i=0; i<cnt && st==A_OK; i++) {
        st = node_abort(n->dns[i]);
    }
    node_trav(n, -1);

    if (n->hdr.type == A_XTASK) {
        ATask* t = (ATask*) n;
        if (t->status == T_SUSPENDED) {
            AStatus st2 = task_resume(t, R_ABORT);
            if (st == A_OK) {
                st = st2;
            }
        } else if (t->status == T_RUNNING) {
            t->aborted = 1;
        }
    }
    return st;
}

// The running task was aborted by what it just did.
static AStatus chk_aborted (AStatus st) {
    if (st==A_OK && a_cur!=NULL && a_cur->aborted) {
        return A_ABORT;
    }
    return st;
}

AStatus a_abort (AVal v) {
    if (v.type!=A_XTASK && v.type!=A_TASKS) {
        return a_error("invalid abort : expected task");
    }
    return chk_aborted(node_abort((ANode*)v.u.o));
}

///////////////////////////////////////////////////////////////////////////////
// EMIT
///////////////////////////////////////////////////////////////////////////////

// Traverses a subtree: the children in order, then the node itself at
// its await.
static AStatus emit_node (ANode* n, uint32_t time, AVal evt) {
    if (n->dead) {
        return A_OK;
    }
    AVal me = a_obj(&n->hdr);
    a_inc(me);

    AStatus st = A_OK;
    node_trav(n, 1);
    int cnt = n->ndns;
    for (int i=0; i<cnt && st==A_OK; i++) {
        st = emit_node(n->dns[i], time, evt);
    }
    node_trav(n, -1);

    if (n->hdr.type == A_XTASK) {
        ATask* t = (ATask*) n;
        if (st != A_OK) {
            // a child exited: raised at the await of the parent, or
            // propagated if the parent is on the C stack
            if (t->status == T_SUSPENDED) {
                t->exit = st;
                st = task_resume(t, R_EXIT);
            }
        } else if (t->status==T_SUSPENDED && t->time<time &&
                   patt_match(t->awt,evt))
        {
            st = task_resume(t, R_EVENT);
        }
    }

    a_dec(me);
    return st;
}

static AStatus emit_top (ANode* n, AVal evt) {
    return emit_node(n, ++TIME, evt);
}

// Target of an emit: :task (default), :parent, :global, a task or
// pool, or a number of levels up.
static AStatus emit_to (ANode** ret, AVal to) {
    ATask* me = task_of((ANode*)a_cur);
    ANode* n  = (me != NULL) ? &me->node : &ROOT.node;
    double lvl = 0;

    if (to.type == A_NIL) {
        // default
    } else if (to.type==A_XTASK || to.type==A_TASKS) {
        *ret = (ANode*) to.u.o;
        return A_OK;
    } else if (to.type == A_NUM) {
        lvl = to.u.n;
    } else if (to.type==A_STR && !strcmp(A_STR_OF(to)->s,"task")) {
        // default
    } else if (to.type==A_STR && !strcmp(A_STR_OF(to)->s,"parent")) {
        lvl = 1;
    } else if (to.type==A_STR && !strcmp(A_STR_OF(to)->s,"global")) {
        *ret = &ROOT.node;
        return A_OK;
    } else {
        return a_error("invalid emit : invalid target");
    }

    // transparent tasks are invisible, pools count
    for (; lvl>0; lvl--) {
        do {
            n = n->up;
            if (n == NULL) {
                return a_error("invalid emit : invalid target");
            }
        } while (n->hdr.type==A_XTASK && ((ATask*)n)->tra);
    }
    *ret = n;
    return A_OK;
}

AStatus a_emit (AVal to, AVal evt) {
    ANode* n = NULL;
    AStatus st = emit_to(&n, to);
    if (st != A_OK) {
        return st;
    }
    return chk_aborted(emit_top(n, evt));
}

///////////////////////////////////////////////////////////////////////////////
// SPAWN
///////////////////////////////////////////////////////////////////////////////

static int pool_live (ATasks* ts) {
    int n = 0;
    for (int i=0; i<ts->node.ndns; i++) {
        n += !ts->node.dns[i]->dead;
    }
    return n;
}

// New task under `up`, with the arguments at `[base,a_top)`.
static ATask* task_new (AFunc* f, ANode* up, int base) {
    const AProto* p = f->task;
    ATask* t = calloc(1, sizeof(ATask));
    node_init(&t->node, A_XTASK);
    t->proto = f;
    a_inc(a_obj(&f->hdr));
    t->L = calloc(p->nslots, sizeof(AVal));
    t->status = T_RUNNING;

    // parameters, then a table with the extra arguments
    int argc = a_top - base;
    for (int i=0; i<p->npar && i<argc; i++) {
        a_cpy(&t->L[i], a_stk[base+i]);
    }
    if (p->dots) {
        t->L[p->npar] = a_tab();
        for (int i=p->npar; i<argc; i++) {
            a_tab_set(A_TAB_OF(t->L[p->npar]), a_num(i - p->npar),
                a_stk[base+i]);
        }
    }
    a_settop(base);

    node_add(up, &t->node);
    return t;
}

AStatus a_spawn (AVal* dst, AVal proto, AVal pool, int base) {
    if (proto.type != A_TASK) {
        a_settop(base);
        return a_error("invalid spawn : expected task prototype");
    }

    ANode* up = (a_cur != NULL) ? &a_cur->node : &ROOT.node;
    if (pool.type == A_TASKS) {
        ATasks* ts = A_TASKS_OF(pool);
        up = &ts->node;
        if (ts->max>=0 && pool_live(ts)>=ts->max) {
            a_settop(base);
            a_clr(dst);
            return A_OK;
        }
    } else if (pool.type != A_NIL) {
        a_settop(base);
        return a_error("invalid spawn : expected tasks pool");
    }

    ATask* t = task_new(A_FUNC_OF(proto), up, base);
    // a task in a pool is owned by the pool
    t->node.hdr.pinned = (pool.type == A_TASKS);
    a_cpy(dst, a_obj(&t->node.hdr));
    return chk_aborted(task_resume(t, R_START));
}

AStatus a_tasks (AVal* dst, AVal max) {
    if (max.type!=A_NIL && max.type!=A_NUM) {
        return a_error("invalid tasks limit : expected number");
    }
    ATasks* ts = calloc(1, sizeof(ATasks));
    node_init(&ts->node, A_TASKS);
    ts->max = (max.type == A_NUM) ? (int)max.u.n : -1;
    node_add((a_cur != NULL) ? &a_cur->node : &ROOT.node, &ts->node);
    a_cpy(dst, a_obj(&ts->node.hdr));
    return A_OK;
}

///////////////////////////////////////////////////////////////////////////////
// AWAIT
///////////////////////////////////////////////////////////////////////////////

AStatus a_await_val (ATask* t, AVal v) {
    if (t->aborted) {
        return A_ABORT;
    }
    APatt* p = NULL;
    AStatus st = patt_val(&p, v);
    if (st != A_OK) {
        return st;
    }
    if (patt_start(p)) {
        patt_free(p);
        return A_OK;
    }
    t->awt  = p;
    t->time = TIME;
    return A_AWAIT;
}

AStatus a_resumed (ATask* t, AKind k) {
    patt_free(t->awt);
    t->awt = NULL;
    switch (k) {
        case R_ABORT: return A_ABORT;
        case R_EXIT:  return t->exit;
        default:      return A_OK;
    }
}

///////////////////////////////////////////////////////////////////////////////
// ME / PUB / PIN
///////////////////////////////////////////////////////////////////////////////

AVal a_me (void) {
    ATask* me = task_of((ANode*)a_cur);
    if (me == NULL) {
        return a_nil();
    }
    AVal v = a_obj(&me->node.hdr);
    a_inc(v);
    return v;
}

AStatus a_pub (AVal** ref) {
    ATask* me = task_of((ANode*)a_cur);
    if (me == NULL) {
        return a_error("invalid pub : expected enclosing task");
    }
    *ref = &me->pub;
    return A_OK;
}

AStatus a_pin (AVal* slot) {
    AVal v = *slot;
    if (v.type!=A_XTASK && v.type!=A_TASKS) {
        return A_OK;
    }
    if (v.u.o->pinned) {
        return a_error("invalid assignment : unexpected pinned value");
    }
    // taking the running task or an ancestor would abort upwards
    if (is_up((ANode*)v.u.o)) {
        return a_error("invalid assignment : unexpected parent task");
    }
    v.u.o->pinned = 1;
    return A_OK;
}

AStatus a_chk_task (AVal v) {
    // the running task and its ancestors outlive the current scope
    if (v.u.o->pinned || is_up((ANode*)v.u.o)) {
        return A_OK;
    }
    return a_error("invalid assignment : expected pinned value");
}

AStatus a_pin_end (AVal* slot, AStatus st) {
    AVal v = *slot;
    if (v.type!=A_XTASK && v.type!=A_TASKS) {
        return st;
    }
    ANode* n = (ANode*) v.u.o;

    // the abort runs defers: the pending exit is set aside
    APay pay;
    a_pay_save(&pay);
    AStatus st2 = node_abort(n);
    if (st2 == A_OK) {
        a_pay_restore(&pay);
    } else {
        a_pay_drop(&pay);
        st = st2;
    }

    // a pool leaves the tree with its block
    if (n->hdr.type==A_TASKS && !n->dead) {
        node_died(n);
    }
    return chk_aborted(st);
}

///////////////////////////////////////////////////////////////////////////////
// MAIN
///////////////////////////////////////////////////////////////////////////////

AStatus a_main (const AProto* p, int argc, AVal* argv) {
    node_init(&ROOT.node, A_TASKS);
    ROOT.max = -1;
    TIME = 1;

    AVal proto = a_task(p, 0);
    int base = a_top;
    for (int i=0; i<argc; i++) {
        a_pushc(argv[i]);
    }
    ATask* t = task_new(A_FUNC_OF(proto), &ROOT.node, base);
    AVal main = a_obj(&t->node.hdr);
    a_inc(main);
    a_dec(proto);

    AStatus st = task_resume(t, R_START);

    // nothing can awake the main chunk: it is aborted
    if (st==A_OK && t->status!=T_DONE) {
        st = node_abort(&t->node);
    }

    a_dec(main);
    node_sweep(&ROOT.node);
    free(ROOT.node.dns);
    ROOT.node.dns = NULL;
    return st;
}
