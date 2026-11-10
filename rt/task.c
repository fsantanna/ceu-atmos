// Tasks: tree, spawn, resume, emit, await, abort.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "atmos.h"

ATask* a_cur = NULL;
void*  a_join_key = NULL;

// Root of the task tree.
static ATasks ROOT;

// Emit counter: a task only reacts to emits that start after its
// current await was established.
static uint32_t TIME = 1;

// Clock: time to the nearest timer (a lower bound, reported by the
// timers themselves), and what is left of the current tick after the
// piece in progress.
static double CLK_MIN  = INFINITY;
static double CLK_LATE = 0;

// Main chunk, and the program arguments.
static ATask* MAIN = NULL;
static int    ARGC = 0;
static char** ARGV = NULL;

static AStatus node_abort (ANode* n);
static AStatus task_resume (ATask* t, AKind k);
static void    wait_set (ATask* t, AStatus st);
static AStatus emit_top   (ANode* n, AVal evt);
static AStatus emit_self  (ANode* n, uint32_t time, AVal evt);

// The running task was aborted by what it just did. It is told only
// once: from then on it unwinds, and its defers execute normally.
static AStatus chk_aborted (AStatus st) {
    if (st==A_OK && a_cur!=NULL && a_cur->aborted==1) {
        a_cur->aborted = 2;
        return A_ABORT;
    }
    return st;
}

///////////////////////////////////////////////////////////////////////////////
// PATTERNS
///////////////////////////////////////////////////////////////////////////////

typedef enum {
    // value, classified when the await starts
    PK_VAL,
    // any event / never
    PK_TRUE, PK_FALSE,
    // event ?? value
    PK_IS,
    // termination of a task
    PK_TASK,
    // T(...): becomes PK_TASK when the await starts
    PK_SPAWN,
    // [p] until (f) / [p] while (f)
    PK_UNTIL, PK_WHILE,
    // !p / p && q / p || q
    PK_NOT, PK_AND, PK_OR,
    // :any ts / :all ts
    PK_ANY, PK_ALL,
    // duration / any clock tick
    PK_CLK, PK_TICK
} PKind;

struct APatt {
    PKind  kind;
    // value, task, prototype, or predicate
    AVal   v;
    // arguments of a spawn
    AVal   args;
    // the task was spawned by the pattern, which aborts it at the end
    int    own;
    APatt* a;
    APatt* b;
    // duration: time left
    double clk;
    // &&: operands already satisfied, and their first results
    int    da;
    int    db;
    AVal   ra;
    AVal   rb;
};

static APatt* patt_new (PKind kind) {
    APatt* p = calloc(1, sizeof(APatt));
    p->kind = kind;
    return p;
}

APatt* a_pt_val (AVal v) {
    APatt* p = patt_new(PK_VAL);
    p->v = v;
    a_inc(v);
    return p;
}

APatt* a_pt_spawn (AVal proto, AVal args) {
    APatt* p = patt_new(PK_SPAWN);
    p->v    = proto;
    p->args = args;
    a_inc(proto);
    a_inc(args);
    return p;
}

APatt* a_pt_pred (int until, APatt* base, AVal f) {
    APatt* p = patt_new(until ? PK_UNTIL : PK_WHILE);
    p->a = base;
    p->v = f;
    a_inc(f);
    return p;
}

APatt* a_pt_pool (int all, AVal ts) {
    APatt* p = patt_new(all ? PK_ALL : PK_ANY);
    p->v = ts;
    a_inc(ts);
    return p;
}

APatt* a_pt_not (APatt* a) {
    APatt* p = patt_new(PK_NOT);
    p->a = a;
    return p;
}

APatt* a_pt_bin (int and, APatt* a, APatt* b) {
    APatt* p = patt_new(and ? PK_AND : PK_OR);
    p->a = a;
    p->b = b;
    return p;
}

// Aborts the tasks the pattern spawned.
static void patt_stop (APatt* p) {
    if (p == NULL) {
        return;
    }
    if (p->kind==PK_TASK && p->own) {
        node_abort((ANode*)p->v.u.o);
    }
    patt_stop(p->a);
    patt_stop(p->b);
}

static void patt_free (APatt* p) {
    if (p == NULL) {
        return;
    }
    patt_stop(p);
    patt_free(p->a);
    patt_free(p->b);
    a_dec(p->v);
    a_dec(p->args);
    a_dec(p->ra);
    a_dec(p->rb);
    free(p);
}

static void pay1 (AVal v) {
    a_pay_clear();
    a_pay_addc(v);
}

static void pay_task (AVal t) {
    a_pay_clear();
    a_pay_addc(A_TASK_OF(t)->ret);
    a_pay_addc(t);
}

// Calls a predicate on a value. The result is the first value
// returned (owned).
static AStatus pred (AVal f, AVal it, AVal* res) {
    int base = a_top;
    a_pushc(it);
    AStatus st = a_call(f, base);
    if (st != A_OK) {
        return st;
    }
    *res = a_nil();
    a_ret1(res, base);
    return A_OK;
}

// The base of an until/while was satisfied with `it` (the first value
// in the payload): applies the predicate.
//  - until: holds if the result does, which replaces `it` unless true
//  - while: holds if the result does not, with `it`
static AStatus patt_pred (APatt* p, int* done) {
    AVal it = a_nil();
    a_pay_take(&it, 0);
    a_pay_clear();
    AVal res;
    AStatus st = pred(p->v, it, &res);
    if (st != A_OK) {
        a_dec(it);
        return st;
    }
    int ok = a_truthy(res);
    if (p->kind == PK_UNTIL) {
        *done = ok;
        if (ok) {
            pay1((res.type==A_BOOL) ? it : res);
        }
    } else {
        *done = !ok;
        if (!ok) {
            pay1(it);
        }
    }
    a_dec(it);
    a_dec(res);
    return A_OK;
}

static AStatus patt_start (APatt* p, int* done);

// A timer expires with how late it is, or reports the time it still
// needs.
static void patt_clk (APatt* p, int* done) {
    if (p->clk <= 0) {
        pay1(a_numi(CLK_LATE - p->clk));
        *done = 1;
    } else if (p->clk < CLK_MIN) {
        CLK_MIN = p->clk;
    }
}

// Starts an until/while again after its predicate rejected the base.
static AStatus patt_again (APatt* p) {
    int done = 0;
    patt_stop(p->a);
    AStatus st = patt_start(p->a, &done);
    // a base that is satisfied at once waits for the next event
    a_pay_clear();
    return st;
}

// Establishes a pattern. Sets `done` if it is satisfied before any
// event, with the results in the payload.
static AStatus patt_start (APatt* p, int* done) {
    AStatus st = A_OK;
    *done = 0;
    switch (p->kind) {
        case PK_VAL:
            switch (p->v.type) {
                case A_BOOL:
                    p->kind = p->v.u.b ? PK_TRUE : PK_FALSE;
                    break;
                case A_TAB:
                    p->kind = PK_IS;
                    break;
                case A_STR:
                    p->kind = !strcmp(A_STR_OF(p->v)->s,"clock") ?
                        PK_TICK : PK_IS;
                    break;
                case A_INT:
                case A_NUM:
                    p->kind = PK_CLK;
                    return patt_start(p, done);
                case A_XTASK:
                    p->kind = PK_TASK;
                    return patt_start(p, done);
                case A_NIL:
                    return a_error("invalid await : invalid event pattern");
                case A_TASKS:
                    return a_error("invalid await : unexpected tasks pool");
                case A_FUNC:
                    return a_error("invalid await : unexpected function");
                default:
                    return a_error("invalid await : unexpected %s",
                        a_typename(p->v));
            }
            break;

        case PK_CLK:
            // a timer counts down from its full duration
            p->clk = a_dbl(p->v);
            patt_clk(p, done);
            break;

        case PK_TASK:
            if (A_TASK_OF(p->v)->status == T_DONE) {
                pay_task(p->v);
                *done = 1;
            }
            break;

        case PK_SPAWN: {
            // the task runs inside the awaiting task
            if (p->v.type != A_TASK) {
                return a_error("invalid spawn : expected task prototype");
            }
            int base = a_top;
            a_args_unpack(p->args);
            AVal t = a_nil();
            st = a_spawn(&t, p->v, a_nil(), base);
            a_set(&p->v, t);
            p->kind = PK_TASK;
            p->own  = 1;
            if (st != A_OK) {
                return st;
            }
            return patt_start(p, done);
        }

        case PK_UNTIL:
        case PK_WHILE:
            if (p->a == NULL) {
                // the predicate alone is first tested with no event
                pay1(a_nil());
                return patt_pred(p, done);
            }
            st = patt_start(p->a, done);
            if (st==A_OK && *done) {
                st = patt_pred(p, done);
                if (st==A_OK && !*done) {
                    st = patt_again(p);
                }
            }
            break;

        case PK_NOT:
            st = patt_start(p->a, done);
            *done = 0;
            a_pay_clear();
            break;

        case PK_AND:
            st = patt_start(p->a, &p->da);
            if (st==A_OK && p->da) {
                a_pay_take(&p->ra, 0);
            }
            if (st == A_OK) {
                st = patt_start(p->b, &p->db);
            }
            if (st==A_OK && p->db) {
                a_pay_take(&p->rb, 0);
            }
            if (st==A_OK && p->da && p->db) {
                a_pay_clear();
                a_pay_addc(p->ra);
                a_pay_addc(p->rb);
                *done = 1;
            }
            break;

        case PK_OR:
            st = patt_start(p->a, done);
            if (st==A_OK && !*done) {
                st = patt_start(p->b, done);
            }
            break;

        case PK_ANY:
        case PK_ALL:
            if (p->v.type != A_TASKS) {
                return a_error("invalid await : expected tasks pool");
            }
            break;

        default:
            break;
    }
    return st;
}

// Tests an event against a pattern. Sets `done` if it is satisfied,
// with the results in the payload.
static AStatus patt_match (APatt* p, AVal evt, int* done) {
    AStatus st = A_OK;
    *done = 0;
    switch (p->kind) {
        case PK_TRUE:
            *done = 1;
            break;
        case PK_IS:
            *done = a_is(evt, p->v);
            break;
        case PK_TASK:
            if (a_eq(evt, p->v)) {
                pay_task(p->v);
                *done = 1;
            }
            return A_OK;

        case PK_UNTIL:
        case PK_WHILE:
            if (p->a == NULL) {
                pay1(evt);
                return patt_pred(p, done);
            }
            st = patt_match(p->a, evt, done);
            if (st==A_OK && *done) {
                st = patt_pred(p, done);
                if (st==A_OK && !*done) {
                    st = patt_again(p);
                }
            }
            return st;

        case PK_NOT:
            st = patt_match(p->a, evt, done);
            if (st != A_OK) {
                return st;
            }
            if (*done) {
                // the operand matched: it starts again
                *done = 0;
                patt_stop(p->a);
                st = patt_start(p->a, done);
                *done = 0;
                a_pay_clear();
                return st;
            }
            *done = 1;
            break;

        case PK_AND:
            if (!p->da) {
                st = patt_match(p->a, evt, &p->da);
                if (st==A_OK && p->da) {
                    a_pay_take(&p->ra, 0);
                }
            }
            if (st==A_OK && !p->db) {
                st = patt_match(p->b, evt, &p->db);
                if (st==A_OK && p->db) {
                    a_pay_take(&p->rb, 0);
                }
            }
            if (st==A_OK && p->da && p->db) {
                a_pay_clear();
                a_pay_addc(p->ra);
                a_pay_addc(p->rb);
                *done = 1;
            }
            return st;

        case PK_OR:
            st = patt_match(p->a, evt, done);
            if (st==A_OK && !*done) {
                st = patt_match(p->b, evt, done);
            }
            return st;

        case PK_CLK:
            if (a_isnum(evt)) {
                p->clk -= a_dbl(evt);
                patt_clk(p, done);
            }
            return A_OK;

        case PK_TICK:
            if (a_isnum(evt)) {
                a_pay_clear();
                a_pay_addc(evt);
                a_pay_add(a_numi(CLK_LATE));
                *done = 1;
            }
            return A_OK;

        case PK_ANY:
        case PK_ALL:
            // a task of the pool terminated (and it was the last one)
            if (evt.type==A_XTASK && A_TASK_OF(evt)->pool==A_TASKS_OF(p->v)) {
                if (p->kind==PK_ANY || a_tasks_len(A_TASKS_OF(p->v))==0) {
                    pay_task(evt);
                    a_pay_addc(p->v);
                    *done = 1;
                }
            }
            return A_OK;

        default:
            break;
    }
    if (*done) {
        pay1(evt);
    }
    return st;
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

// Releases the frame of a task that terminated.
static void frame_dec (ATask* t) {
    AFrame* f = t->frame;
    t->frame = NULL;
    t->L = NULL;
    if (f!=NULL && --f->rc==0) {
        a_clrs(f->L, f->n);
        free(f);
    }
}

// Releases the toggle state of a node.
static void node_untoggle (ANode* n) {
    for (int i=0; i<n->nfilt; i++) {
        patt_free(n->filt[i]);
    }
    free(n->filt);
    n->filt  = NULL;
    n->nfilt = 0;
    n->toggled = 0;
}

void a_node_free (ANode* n) {
    node_untoggle(n);
    a_dec(n->tog);
    if (n->hdr.type == A_XTASK) {
        ATask* t = (ATask*) n;
        patt_free(t->awt);
        frame_dec(t);
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

// Depth of the block that a task or pool is attached to: its pin
// block, or the block of the spawn while it is not pinned.
static int node_adepth (ANode* n) {
    if (n->hdr.type==A_XTASK && ((ATask*)n)->tra) {
        return ((ATask*)n)->bdepth - 1;
    } else if (n->hdr.type==A_XTASK && ((ATask*)n)->pool!=NULL) {
        // a task in a pool follows the pin of the pool
        return node_adepth(&((ATask*)n)->pool->node);
    } else if (n->hdr.pinned) {
        return n->hdr.depth;
    } else if (n->hdr.type == A_XTASK) {
        return ((ATask*)n)->depth - 1;
    } else {
        return 0;
    }
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

    // its blocks already aborted the children: these are leftovers,
    // such as a pool that was never pinned
    node_trav(&t->node, 1);
    for (int i=0; i<t->node.ndns; i++) {
        ANode* dn = t->node.dns[i];
        node_abort(dn);
        if (dn->hdr.type==A_TASKS && !dn->dead) {
            node_died(dn);
        }
    }
    node_trav(&t->node, -1);

    // the frame goes with the last task that shares it; the header
    // stays
    frame_dec(t);
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
            st = emit_top((owner != NULL) ? &owner->node : &ROOT.node,
                a_obj(&t->node.hdr));
            break;
        case A_ABORT:
            st = A_OK;
            break;
        default:
            break;
    }

    // a block waited for this task to go on with its exit: an exit of
    // the task replaces the pending one, as if it had ended in time
    ATask* w = t->waiter;
    if (w != NULL) {
        t->waiter = NULL;
        if (st != A_OK) {
            wait_set(w, st);
        }
        st = task_resume(w, R_LATE);
    }
    return st;
}

static AStatus task_resume (ATask* t, AKind k) {
    ATask* prev = a_cur;
    AVal me = a_obj(&t->node.hdr);
    a_inc(me);
    a_cur = t;
    t->status = T_RUNNING;
    int lin   = a_lin;
    int depth = a_depth;
    AStatus st = t->desc->resume(t, k);
    a_lin   = lin;
    a_depth = depth;
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

// Replaces the exit that a waiting task keeps: the payload becomes
// the current one.
static void wait_set (ATask* t, AStatus st) {
    a_pay_drop(&t->wpay);
    for (int i=0; i<t->wn; i++) {
        a_dec(t->wstk[i]);
    }
    free(t->wstk);
    t->wstk = NULL;
    t->wn   = 0;
    a_pay_save(&t->wpay);
    t->wst = st;
}

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
        if (t->waiting) {
            // already leaving a block: goes on as an abort
            if (t->wst != A_ABORT) {
                wait_set(t, A_ABORT);
            }
        } else if (t->status == T_SUSPENDED) {
            AStatus st2 = task_resume(t, R_ABORT);
            if (st == A_OK) {
                st = st2;
            }
        } else if (t->status==T_RUNNING && !t->aborted) {
            // unwinds when control returns to it
            t->aborted = 1;
        }
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

static AStatus emit_node (ANode* n, uint32_t time, AVal evt);

// Starts the filters of a node that was toggled off.
static AStatus toggle_off (ANode* n) {
    n->toggled = 1;
    AStatus st = A_OK;
    for (int i=0; i<n->nfilt && st==A_OK; i++) {
        int done = 0;
        st = patt_start(n->filt[i], &done);
        a_pay_clear();
    }
    return st;
}

// A node toggled on receives a zero tick, which awakes `await(true)`
// and lets its timers report again.
static AStatus toggle_on (ANode* n) {
    n->toggled = 0;
    return emit_node(n, ++TIME, a_int(0));
}

// A toggled-off node only reacts to events that match a filter,
// which then starts again.
static AStatus toggle_pass (ANode* n, AVal evt, int* pass) {
    AStatus st = A_OK;
    *pass = 0;
    for (int i=0; i<n->nfilt && st==A_OK; i++) {
        int done = 0;
        st = patt_match(n->filt[i], evt, &done);
        if (st==A_OK && done) {
            *pass = 1;
            patt_stop(n->filt[i]);
            st = patt_start(n->filt[i], &done);
        }
        a_pay_clear();
    }
    return st;
}

// The event is `:T [b]` for the tag of an automatic toggle.
static int toggle_evt (ANode* n, AVal evt, int* on) {
    if (n->tog.type==A_NIL || evt.type!=A_TAB) {
        return 0;
    }
    AVal k = a_strz("tag");
    AVal tag = a_tab_get(A_TAB_OF(evt), k);
    a_dec(k);
    AVal b = a_tab_get(A_TAB_OF(evt), a_int(0));
    if (b.type!=A_BOOL || !a_gte(n->tog, tag)) {
        return 0;
    }
    *on = b.u.b;
    return 1;
}

// Traverses a subtree: the children in order, then the node itself at
// its await.
static AStatus emit_node (ANode* n, uint32_t time, AVal evt) {
    if (n->dead || n->doomed) {
        return A_OK;
    }
    AVal me = a_obj(&n->hdr);
    a_inc(me);

    AStatus st = A_OK;
    int pass = 1;
    if (n->toggled) {
        st = toggle_pass(n, evt, &pass);
    }
    if (st==A_OK && pass) {
        st = emit_self(n, time, evt);
    }

    // automatic toggle, after the node has seen the event
    int on;
    if (st==A_OK && !n->dead && toggle_evt(n,evt,&on)) {
        if (on && n->toggled) {
            st = toggle_on(n);
        } else if (!on && !n->toggled) {
            st = toggle_off(n);
        }
    }

    a_dec(me);
    return st;
}

static AStatus emit_self (ANode* n, uint32_t time, AVal evt) {
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
            if (t->waiting) {
                // leaving a block: replaces the pending exit
                wait_set(t, st);
                st = A_OK;
            } else if (t->status == T_SUSPENDED) {
                t->exit = st;
                st = task_resume(t, R_EXIT);
            }
        } else if (t->status==T_SUSPENDED && !t->waiting && t->time<time) {
            // predicates run in the context of the awaiting task
            int done  = 0;
            int depth = a_depth;
            ATask* prev = a_cur;
            a_cur   = t;
            a_depth = t->adepth;
            st = patt_match(t->awt, evt, &done);
            a_cur   = prev;
            a_depth = depth;
            if (st != A_OK) {
                t->exit = st;
                st = task_resume(t, R_EXIT);
            } else if (done) {
                st = task_resume(t, R_EVENT);
            }
        }
    }
    return st;
}

AStatus a_toggle (AVal t, AVal on, APatt** fs, int n) {
    AStatus st = A_OK;
    ANode* x = (ANode*) t.u.o;
    if (t.type!=A_XTASK && t.type!=A_TASKS) {
        st = a_error("invalid toggle : expected task");
    } else if (on.type != A_BOOL) {
        st = a_error("invalid toggle : expected bool argument");
    } else if (on.u.b && n>0) {
        st = a_error("invalid toggle : unexpected argument");
    } else if (on.u.b && !x->toggled) {
        st = a_error("invalid toggle : expected toggled off task");
    } else if (!on.u.b && x->toggled) {
        st = a_error("invalid toggle : expected awaiting task");
    }
    if (st != A_OK) {
        for (int i=0; i<n; i++) {
            patt_free(fs[i]);
        }
        return st;
    }

    if (on.u.b) {
        node_untoggle(x);
        return chk_aborted(toggle_on(x));
    }
    x->filt = malloc((n > 0 ? n : 1) * sizeof(APatt*));
    memcpy(x->filt, fs, n * sizeof(APatt*));
    x->nfilt = n;
    return toggle_off(x);
}

void a_toggle_auto (AVal t, AVal tag, APatt** fs, int n) {
    ANode* x = (ANode*) t.u.o;
    a_cpy(&x->tog, tag);
    x->filt = malloc((n > 0 ? n : 1) * sizeof(APatt*));
    memcpy(x->filt, fs, n * sizeof(APatt*));
    x->nfilt = n;
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
    } else if (a_isnum(to)) {
        lvl = a_dbl(to);
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

// A tick is split in pieces, each ending at the nearest timer, so
// timers expire in order within a single tick.
static AStatus emit_tick (double us) {
    AStatus st = A_OK;
    double late = CLK_LATE;
    do {
        double d = (CLK_MIN < us) ? CLK_MIN : us;
        us -= d;
        // the timers report again
        CLK_MIN  = INFINITY;
        CLK_LATE = us;
        st = emit_top(&ROOT.node, a_numi(d));
    } while (st==A_OK && us>0);
    CLK_LATE = late;
    return st;
}

// A pinned event only reaches tasks that live inside its pin block.
// Depths only compare along a chain of blocks, so the target must be
// in the subtree of the task that owns the pin block.
static AStatus emit_chk (ANode* n, AVal evt) {
    int p = a_pdepth(evt);
    if (p == 0) {
        return A_OK;
    }
    // owner: the first task upwards that starts outside the pin block
    ANode* own = (ANode*) a_cur;
    while (own != NULL) {
        if (own->hdr.type==A_XTASK && ((ATask*)own)->bdepth<=p) {
            break;
        }
        own = own->up;
    }
    if (own == NULL) {
        return a_err_pinned();
    }
    // the owner itself: only if pinned in its outermost block
    if (n == own) {
        return (((ATask*)own)->bdepth == p) ? A_OK : a_err_pinned();
    }
    // otherwise, a subtree attached inside the pin block
    ANode* c = n;
    while (c!=NULL && c->up!=own) {
        c = c->up;
    }
    if (c==NULL || node_adepth(c)<p) {
        return a_err_pinned();
    }
    return A_OK;
}

AStatus a_emit (AVal to, AVal evt) {
    ANode* n = NULL;
    AStatus st = emit_to(&n, to);
    if (st != A_OK) {
        return st;
    }
    if (a_isnum(evt)) {
        // time passes for the whole program
        if (n != &ROOT.node) {
            return a_error("invalid emit : expected global");
        }
        return chk_aborted(emit_tick(a_dbl(evt)));
    }
    if (emit_chk(n,evt) != A_OK) {
        return A_ERROR;
    }
    return chk_aborted(emit_top(n, evt));
}

double a_clock_next (void) {
    return (CLK_MIN < INFINITY) ? CLK_MIN : -1;
}

///////////////////////////////////////////////////////////////////////////////
// SPAWN
///////////////////////////////////////////////////////////////////////////////

int a_tasks_len (ATasks* ts) {
    int n = 0;
    for (int i=0; i<ts->node.ndns; i++) {
        n += !ts->node.dns[i]->dead;
    }
    return n;
}

ATask* a_tasks_next (ATasks* ts, int* pos) {
    while (*pos < ts->node.ndns) {
        ANode* n = ts->node.dns[(*pos)++];
        if (!n->dead) {
            return (ATask*) n;
        }
    }
    return NULL;
}

AVal a_tasks_tab (AVal ts) {
    AVal t = a_tab();
    int pos = 0;
    ATask* x;
    while ((x = a_tasks_next(A_TASKS_OF(ts), &pos)) != NULL) {
        a_tab_set(A_TAB_OF(t), a_int(A_TAB_OF(t)->an), a_obj(&x->node.hdr));
    }
    return t;
}

// New task under `up`, with the arguments at `[base,a_top)`.
static ATask* task_new (AFunc* f, ANode* up, int base) {
    const AProto* p = f->task;
    ATask* t = calloc(1, sizeof(ATask));
    node_init(&t->node, A_XTASK);
    t->desc  = p;
    t->proto = f;
    a_inc(a_obj(&f->hdr));
    t->frame = calloc(1, sizeof(AFrame) + p->nslots*sizeof(AVal));
    t->frame->rc = 1;
    t->frame->n  = p->nslots;
    t->L = t->frame->L;
    t->status = T_RUNNING;
    // a task is confined like its prototype, if it is a closure
    t->mindepth = a_pdepth(a_obj(&f->hdr));

    // parameters, then a table with the extra arguments
    int argc = a_top - base;
    for (int i=0; i<p->npar && i<argc; i++) {
        a_cpy(&t->L[i], a_stk[base+i]);
    }
    if (p->dots) {
        // the extra arguments wait in a table
        int n = (argc > p->npar) ? argc-p->npar : 0;
        t->L[p->npar] = a_tab();
        for (int i=0; i<n; i++) {
            a_pushc(a_stk[base+p->npar+i]);
        }
        a_args_pack(t->L[p->npar], n);
    }
    // a task that receives pinned arguments cannot leave their blocks
    for (int i=0; i<argc; i++) {
        int d = a_pdepth(a_stk[base+i]);
        if (d > t->mindepth) {
            t->mindepth = d;
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
        // the task lives as long as the pool: so must its prototype
        // and its arguments
        if (a_chk_store(proto, a_pdepth(pool)) != A_OK) {
            a_settop(base);
            return A_ERROR;
        }
        for (int i=base; i<a_top; i++) {
            if (a_chk_store(a_stk[i], a_pdepth(pool)) != A_OK) {
                a_settop(base);
                return A_ERROR;
            }
        }
        if (ts->max>=0 && a_tasks_len(ts)>=ts->max) {
            a_settop(base);
            a_clr(dst);
            return A_OK;
        }
    } else if (pool.type != A_NIL) {
        a_settop(base);
        return a_error("invalid spawn : expected tasks pool");
    }

    ATask* t = task_new(A_FUNC_OF(proto), up, base);
    if (a_cur!=NULL && t->mindepth>a_cur->conf) {
        a_cur->conf = t->mindepth;
    }
    // the body is one deeper than the block the task is attached to
    t->depth = a_depth + 1;
    // a task in a pool is owned by the pool
    if (pool.type == A_TASKS) {
        t->node.hdr.pinned = 1;
        t->node.hdr.depth  = pool.u.o->depth;
        t->pool = A_TASKS_OF(pool);
        if (pool.u.o->pinned) {
            t->depth = pool.u.o->depth + 1;
        }
    }
    t->bdepth = t->depth;
    a_cpy(dst, a_obj(&t->node.hdr));
    return chk_aborted(task_resume(t, R_START));
}

AStatus a_spawn_tra (AVal* dst, ATask* owner, const AProto* p) {
    ATask* t = calloc(1, sizeof(ATask));
    node_init(&t->node, A_XTASK);
    t->tra   = 1;
    t->desc  = p;
    t->proto = owner->proto;
    a_inc(a_obj(&t->proto->hdr));
    t->frame = owner->frame;
    t->frame->rc++;
    t->L = owner->L;
    // a transparent block is a nested block of its owner
    t->depth  = owner->depth;
    t->bdepth = a_depth + 1;
    t->status = T_RUNNING;
    node_add(&owner->node, &t->node);
    a_cpy(dst, a_obj(&t->node.hdr));
    return chk_aborted(task_resume(t, R_START));
}

AStatus a_tasks (AVal* dst, AVal max) {
    if (max.type!=A_NIL && !a_isnum(max)) {
        return a_error("invalid tasks limit : expected number");
    }
    ATasks* ts = calloc(1, sizeof(ATasks));
    node_init(&ts->node, A_TASKS);
    ts->max = a_isnum(max) ? (int)a_dbl(max) : -1;
    node_add((a_cur != NULL) ? &a_cur->node : &ROOT.node, &ts->node);
    a_cpy(dst, a_obj(&ts->node.hdr));
    return A_OK;
}

///////////////////////////////////////////////////////////////////////////////
// AWAIT
///////////////////////////////////////////////////////////////////////////////

AStatus a_await (ATask* t, APatt* p) {
    if (t->aborted) {
        patt_free(p);
        return A_ABORT;
    }
    t->time   = TIME;
    t->adepth = a_depth;
    int done = 0;
    AStatus st = chk_aborted(patt_start(p, &done));
    if (st!=A_OK || done) {
        // the results must survive the abort of spawned tasks
        APay pay;
        a_pay_save(&pay);
        patt_free(p);
        a_pay_restore(&pay);
        return st;
    }
    t->awt = p;
    return A_AWAIT;
}

AStatus a_resumed (ATask* t, AKind k) {
    // the payload (results or exit) must survive the abort of the
    // tasks the pattern spawned
    APatt* p = t->awt;
    t->awt = NULL;
    APay pay;
    a_pay_save(&pay);
    patt_free(p);
    a_pay_restore(&pay);
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

AStatus a_pub_set (ATask* t, AVal v) {
    if (t == NULL) {
        t = task_of((ANode*)a_cur);
        if (t == NULL) {
            return a_error("invalid pub : expected enclosing task");
        }
    }
    // the pub is visible wherever the task is
    if (a_chk_store(v, node_adepth(&t->node)) != A_OK) {
        return A_ERROR;
    }
    a_cpy(&t->pub, v);
    return A_OK;
}

void a_func_scope (AVal f) {
    AFunc* fn = A_FUNC_OF(f);
    int d = 0;
    for (int i=0; i<fn->ncap; i++) {
        int p = a_pdepth(fn->caps[i]);
        if (p > d) {
            d = p;
        }
    }
    if (d > 0) {
        fn->hdr.pinned = 1;
        fn->hdr.depth  = d;
    }
}

int a_pdepth (AVal v) {
    if (v.type < A_TAB) {
        return 0;
    }
    int d = v.u.o->pinned ? (int)v.u.o->depth : 0;
    if (v.type == A_XTASK) {
        ATask* t = A_TASK_OF(v);
        // a terminated task holds nothing and needs no owner
        if (t->node.dead || t->status==T_DONE) {
            return 0;
        }
        // a task in a pool follows the pin of the pool
        if (t->pool != NULL) {
            d = node_adepth(&t->pool->node);
        }
        if (t->mindepth > d) {
            d = t->mindepth;
        }
    }
    return d;
}

AStatus a_pin (AVal* slot) {
    AVal v = *slot;
    switch (v.type) {
        case A_XTASK:
        case A_TASKS:
            if (v.u.o->pinned) {
                return a_error("invalid assignment : unexpected pinned value");
            }
            // taking the running task or an ancestor would abort upwards
            if (is_up((ANode*)v.u.o)) {
                return a_error("invalid assignment : unexpected parent task");
            }
            if (a_pdepth(v) > a_depth) {
                return a_err_pinned();
            }
            break;
        case A_TAB:
        case A_FUNC:
        case A_TASK:
        case A_NAT:
            // The block must hold the only reference. A fresh value
            // may already be confined by what it holds (a closure, or
            // a value that left its block): the pin only narrows it.
            if (v.u.o->rc != 1) {
                return a_error(v.u.o->pinned ?
                    "invalid assignment : unexpected pinned value" :
                    "invalid pin : shared value");
            }
            break;
        default:
            return A_OK;
    }
    v.u.o->pinned = 1;
    v.u.o->depth  = a_depth;
    return A_OK;
}

// Transfer of `v` out of the blocks deeper than `d`. The pinned data
// (tables, closures, natives) reachable from `v` that is pinned deeper
// than `d` leaves with it. Only pinned objects are visited: unpinned
// ones hold no pinned values. Tasks and pools held by the data do not
// leave: they are aborted with their block.

#define XFER UINT32_MAX     // depth of an object being transferred

// Marks what leaves. Returns the deepest pin block among the values
// that stay but are still held by what leaves.
static int xfer_mark (AVal v, int d) {
    if (v.type < A_TAB) {
        return 0;
    }
    AObj* o = v.u.o;
    if (v.type==A_XTASK || v.type==A_TASKS) {
        int p = a_pdepth(v);
        return (p <= d) ? p : 0;
    }
    if (!o->pinned || o->depth==XFER) {
        return 0;
    }
    if ((int)o->depth <= d) {
        return o->depth;
    }
    o->depth = XFER;
    int m = 0;
    if (v.type == A_TAB) {
        uint32_t pos = 0;
        AVal k, x;
        while (a_tab_next(A_TAB_OF(v), &pos, &k, &x)) {
            int mk = xfer_mark(k, d);
            int mx = xfer_mark(x, d);
            m = (mk > m) ? mk : m;
            m = (mx > m) ? mx : m;
        }
    } else if (v.type==A_FUNC || v.type==A_TASK) {
        AFunc* f = A_FUNC_OF(v);
        for (int i=0; i<f->ncap; i++) {
            int mc = xfer_mark(f->caps[i], d);
            m = (mc > m) ? mc : m;
        }
    }
    return m;
}

// What leaves becomes free, or confined to the block `m` of what it
// still holds.
static void xfer_set (AVal v, int m) {
    if (v.type<A_TAB || !v.u.o->pinned || v.u.o->depth!=XFER) {
        return;
    }
    AObj* o = v.u.o;
    o->pinned = (m > 0);
    o->depth  = m;
    if (v.type == A_TAB) {
        uint32_t pos = 0;
        AVal k, x;
        while (a_tab_next(A_TAB_OF(v), &pos, &k, &x)) {
            xfer_set(k, m);
            xfer_set(x, m);
        }
    } else if (v.type==A_FUNC || v.type==A_TASK) {
        AFunc* f = A_FUNC_OF(v);
        for (int i=0; i<f->ncap; i++) {
            xfer_set(f->caps[i], m);
        }
    }
}

// Pinned arguments confine a task to their blocks: it cannot leave.
static int xfer_chk (AVal v, int d) {
    if (v.type == A_XTASK) {
        ATask* t = A_TASK_OF(v);
        int live = (!t->node.dead && t->status!=T_DONE);
        return (t->pool!=NULL || !live || t->mindepth<=d);
    } else if (v.type==A_TASKS && v.u.o->pinned && (int)v.u.o->depth>d) {
        int pos = 0;
        ATask* t;
        while ((t = a_tasks_next(A_TASKS_OF(v),&pos)) != NULL) {
            if (t->mindepth > d) {
                return 0;
            }
        }
    }
    return 1;
}

static void xfer (AVal v, int d) {
    if (v.type < A_TAB) {
        return;
    }
    AObj* o = v.u.o;
    int deep = (o->pinned && (int)o->depth>d);
    switch (v.type) {
        case A_XTASK: {
            ATask* t = A_TASK_OF(v);
            // a task in a pool stays with the pool
            if (t->pool!=NULL || t->node.dead || t->status==T_DONE) {
                return;
            }
            // the destination must pin it again
            if (deep) {
                o->pinned = 0;
            }
            xfer(t->pub, d);
            break;
        }
        case A_TASKS: {
            if (!deep) {
                return;
            }
            int pos = 0;
            ATask* t;
            while ((t = a_tasks_next(A_TASKS_OF(v),&pos)) != NULL) {
                xfer(t->pub, d);
            }
            o->pinned = 0;
            break;
        }
        default:
            xfer_set(v, xfer_mark(v,d));
            break;
    }
}

// Aborts a task or pool at the end of its block. The abort runs
// defers: the pending exit is set aside meanwhile.
static AStatus blk_abort (ANode* n, AStatus st) {
    APay pay;
    a_pay_save(&pay);
    AStatus st2 = node_abort(n);
    if (st2 == A_OK) {
        a_pay_restore(&pay);
    } else {
        a_pay_drop(&pay);
        st = st2;
    }
    return st;
}

AStatus a_blk_xfer (AStatus st, AVal* vs, int n) {
    int d = a_depth - 1;
    if (st != A_OK) {
        vs = a_pay;
        n  = a_pay_n;
    }
    // all or nothing
    int ok = 1;
    for (int i=0; i<n; i++) {
        ok = ok && xfer_chk(vs[i], d);
    }
    if (ok) {
        for (int i=0; i<n; i++) {
            xfer(vs[i], d);
        }
    } else {
        st = a_err_pinned();
    }

    // tasks confined to this block that nobody pinned
    ATask* me = a_cur;
    if (me==NULL || me->conf<a_depth) {
        return st;
    }
    int any = 0;
    for (int i=0; i<me->node.ndns; i++) {
        ANode* c = me->node.dns[i];
        if (c->hdr.type!=A_XTASK || c->dead || c->hdr.pinned) {
            continue;
        }
        ATask* t = (ATask*) c;
        if (!t->tra && t->status!=T_DONE && t->mindepth>=a_depth) {
            st = blk_abort(c, st);
            any = 1;
            // the abort may rearrange the tree: scan again
            i = -1;
        }
    }
    me->conf = d;
    return any ? chk_aborted(st) : st;
}

AStatus a_chk_task (AVal v) {
    // the running task and its ancestors outlive the current scope,
    // and a terminated task needs no owner
    if (v.u.o->pinned || is_up((ANode*)v.u.o)) {
        return A_OK;
    }
    if (v.type==A_XTASK && A_TASK_OF(v)->status==T_DONE) {
        return A_OK;
    }
    return a_error("invalid assignment : expected pinned value");
}

void a_pin_clr (AVal* slot) {
    AVal v = *slot;
    // only what is still pinned in this very block
    if (v.type<A_STR || !v.u.o->pinned || v.u.o->depth!=(uint32_t)a_depth) {
        return;
    }
    if (v.type == A_TAB) {
        a_tab_clear(A_TAB_OF(v));
    } else if (v.type==A_FUNC || v.type==A_TASK) {
        a_clrs(A_FUNC_OF(v)->caps, A_FUNC_OF(v)->ncap);
    } else if (v.type == A_NAT) {
        a_nat_close(v);
    }
}

// A task of the block that has not ended after its abort: it is
// running below in the C stack, or it waits for one that is.
static ATask* late_of (ANode* n) {
    if (n->hdr.type == A_XTASK) {
        ATask* t = (ATask*) n;
        int late = (t->status==T_RUNNING || t->waiting);
        return (!n->dead && late) ? t : NULL;
    }
    for (int i=0; i<n->ndns; i++) {
        ATask* t = late_of(n->dns[i]);
        if (t != NULL) {
            return t;
        }
    }
    return NULL;
}

// Marks the tasks in a subtree that are running, to unwind when
// control returns to them.
static void node_stop (ANode* n) {
    if (n->dead) {
        return;
    }
    if (n->hdr.type == A_XTASK) {
        ATask* t = (ATask*) n;
        if (t->status==T_RUNNING && !t->aborted) {
            t->aborted = 1;
        }
    }
    for (int i=0; i<n->ndns; i++) {
        node_stop(n->dns[i]);
    }
}

void a_pin_doom (AVal* slot) {
    AVal v = *slot;
    if ((v.type!=A_XTASK && v.type!=A_TASKS) ||
        !v.u.o->pinned || v.u.o->depth!=(uint32_t)a_depth)
    {
        return;
    }
    ANode* n = (ANode*) v.u.o;
    n->doomed = 1;
    node_stop(n);
}

// The task suspends the exit of a block until `late` ends. It keeps
// the pending exit meanwhile.
static void wait_for (ATask* me, ATask* late, AStatus st, int base) {
    late->waiter = me;
    me->waiting  = 1;
    me->wst = st;
    a_pay_save(&me->wpay);
    me->wn   = a_top - base;
    me->wstk = malloc((me->wn + 1) * sizeof(AVal));
    memcpy(me->wstk, a_stk+base, me->wn * sizeof(AVal));
    a_top = base;
}

int a_orph_end (ATask* me, AStatus* st, int base) {
    for (int i=0; i<me->node.ndns; i++) {
        ANode* n = me->node.dns[i];
        if (n->dead) {
            continue;
        }
        // the node may leave the tree when it dies
        AVal ref = a_obj(&n->hdr);
        a_inc(ref);
        *st = blk_abort(n, *st);
        ATask* late = late_of(n);
        if (late != NULL) {
            a_dec(ref);
            wait_for(me, late, *st, base);
            return 1;
        }
        if (n->hdr.type==A_TASKS && !n->dead) {
            node_died(n);
        }
        int dead = n->dead;
        a_dec(ref);
        if (dead) {
            // the abort may rearrange the tree: scan again
            i = -1;
        }
    }
    *st = chk_aborted(*st);
    return 0;
}

void a_orph_doom (ATask* me) {
    for (int i=0; i<me->node.ndns; i++) {
        ANode* n = me->node.dns[i];
        if (!n->dead && !n->hdr.pinned) {
            n->doomed = 1;
            node_stop(n);
        }
    }
}

int a_pin_end (ATask* me, AVal* slot, AStatus* st, int base) {
    AVal v = *slot;
    // only what is still pinned in this very block
    if (v.type<A_STR || !v.u.o->pinned || v.u.o->depth!=(uint32_t)a_depth) {
        return 0;
    }

    // data is released later, by `a_pin_clr`
    if (v.type!=A_XTASK && v.type!=A_TASKS) {
        return 0;
    }
    ANode* n = (ANode*) v.u.o;
    *st = blk_abort(n, *st);

    // the block waits for what could not be aborted yet
    ATask* late = (me != NULL) ? late_of(n) : NULL;
    if (late != NULL) {
        wait_for(me, late, *st, base);
        return 1;
    }

    // a pool leaves the tree with its block
    if (n->hdr.type==A_TASKS && !n->dead) {
        node_died(n);
    }
    *st = chk_aborted(*st);
    return 0;
}

AStatus a_pin_resumed (ATask* t) {
    t->waiting = 0;
    for (int i=0; i<t->wn; i++) {
        a_push(t->wstk[i]);
    }
    free(t->wstk);
    t->wstk = NULL;
    t->wn   = 0;
    a_pay_restore(&t->wpay);
    t->wpay.v   = NULL;
    t->wpay.n   = 0;
    t->wpay.cap = 0;
    return t->wst;
}

///////////////////////////////////////////////////////////////////////////////
// MAIN
///////////////////////////////////////////////////////////////////////////////

AStatus a_main (const AProto* p) {
    node_init(&ROOT.node, A_TASKS);
    ROOT.max = -1;
    TIME = 1;
    CLK_MIN  = INFINITY;
    CLK_LATE = 0;

    // the program arguments are the `...` of the main chunk
    AVal proto = a_task(p, 0);
    int base = a_top;
    for (int i=1; i<ARGC; i++) {
        a_push(a_strz(ARGV[i]));
    }
    MAIN = task_new(A_FUNC_OF(proto), &ROOT.node, base);
    MAIN->depth  = 1;
    MAIN->bdepth = 1;
    a_inc(a_obj(&MAIN->node.hdr));
    a_dec(proto);
    return task_resume(MAIN, R_START);
}

///////////////////////////////////////////////////////////////////////////////
// DRIVER
///////////////////////////////////////////////////////////////////////////////

void a_task_init (int argc, char** argv) {
    ARGC = argc;
    ARGV = argv;
    MAIN = NULL;
    a_cur = NULL;
}

void a_task_quit (void) {
    if (MAIN != NULL) {
        a_dec(a_obj(&MAIN->node.hdr));
        MAIN = NULL;
        node_sweep(&ROOT.node);
        free(ROOT.node.dns);
        ROOT.node.dns = NULL;
    }
}

int atm_alive (void) {
    return MAIN!=NULL && MAIN->status!=T_DONE;
}

AStatus atm_stop (void) {
    if (!atm_alive()) {
        return A_OK;
    }
    return node_abort(&MAIN->node);
}

AStatus atm_emit (AVal evt) {
    if (a_isnum(evt)) {
        return emit_tick(a_dbl(evt));
    }
    return emit_top(&ROOT.node, evt);
}

AStatus atm_step (uint32_t ms) {
    return emit_tick(ms * 1000.0);
}

double atm_next (void) {
    return a_clock_next();
}
