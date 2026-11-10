// Tables.

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "atmos.h"

#define EMPTY (-1)

AVal a_tab (void) {
    ATab* t = calloc(1, sizeof(ATab));
    t->hdr.rc   = 1;
    t->hdr.type = A_TAB;
    return a_obj(&t->hdr);
}

void a_tab_free (ATab* t) {
    for (uint32_t i=0; i<t->an; i++) {
        a_dec(t->arr[i]);
    }
    for (uint32_t i=0; i<t->en; i++) {
        a_dec(t->ents[i].k);
        a_dec(t->ents[i].v);
    }
    free(t->arr);
    free(t->ents);
    free(t->idx);
}

///////////////////////////////////////////////////////////////////////////////
// HASH PART
///////////////////////////////////////////////////////////////////////////////

static uint32_t hash (AVal k) {
    uint64_t x;
    switch (k.type) {
        case A_BOOL:
            x = k.u.b + 1;
            break;
        case A_NUM: {
            // 0 and -0 are the same key
            double n = (k.u.n == 0) ? 0 : k.u.n;
            memcpy(&x, &n, sizeof(x));
            break;
        }
        case A_STR:
            return A_STR_OF(k)->hash;
        default:
            x = (uint64_t)(uintptr_t) k.u.o;
            break;
    }
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    return (uint32_t) x;
}

// Position in `idx` of the key, or of the empty slot where it goes.
static uint32_t probe (ATab* t, AVal k) {
    uint32_t i = hash(k) & (t->icap - 1);
    while (1) {
        int32_t e = t->idx[i];
        if (e == EMPTY) {
            return i;
        }
        if (a_eq(t->ents[e].k, k)) {
            return i;
        }
        i = (i + 1) & (t->icap - 1);
    }
}

// Drops the tombstones, keeping the insertion order, and rebuilds the
// index with room for one more entry.
static void rehash (ATab* t) {
    uint32_t n = 0;
    for (uint32_t i=0; i<t->en; i++) {
        if (t->ents[i].k.type != A_NIL) {
            t->ents[n++] = t->ents[i];
        }
    }
    t->en = n;

    uint32_t cap = 4;
    while (cap < (n+1)*2) {
        cap *= 2;
    }
    if (cap != t->icap) {
        t->icap = cap;
        t->ecap = cap / 2;
        t->idx  = realloc(t->idx,  cap * sizeof(int32_t));
        t->ents = realloc(t->ents, t->ecap * sizeof(AEnt));
    }
    for (uint32_t i=0; i<t->icap; i++) {
        t->idx[i] = EMPTY;
    }
    for (uint32_t i=0; i<n; i++) {
        t->idx[probe(t, t->ents[i].k)] = i;
    }
}

static AVal hget (ATab* t, AVal k) {
    if (t->elive == 0) {
        return a_nil();
    }
    int32_t e = t->idx[probe(t, k)];
    return (e == EMPTY) ? a_nil() : t->ents[e].v;
}

// Removes a key, returning its value (owned), or nil.
static AVal hdel (ATab* t, AVal k) {
    if (t->elive == 0) {
        return a_nil();
    }
    int32_t e = t->idx[probe(t, k)];
    if (e == EMPTY) {
        return a_nil();
    }
    // the tombstone keeps its index slot, so probes go through it
    AVal v = t->ents[e].v;
    a_dec(t->ents[e].k);
    t->ents[e].k = a_nil();
    t->ents[e].v = a_nil();
    t->elive--;
    return v;
}

static void hset (ATab* t, AVal k, AVal v) {
    if (v.type == A_NIL) {
        a_dec(hdel(t, k));
        return;
    }
    if (t->icap > 0) {
        int32_t e = t->idx[probe(t, k)];
        if (e != EMPTY) {
            a_cpy(&t->ents[e].v, v);
            return;
        }
    }
    if (t->en == t->ecap) {
        rehash(t);
    }
    a_inc(k);
    a_inc(v);
    t->ents[t->en].k = k;
    t->ents[t->en].v = v;
    t->idx[probe(t, k)] = t->en;
    t->en++;
    t->elive++;
}

///////////////////////////////////////////////////////////////////////////////
// ARRAY PART
///////////////////////////////////////////////////////////////////////////////

// Array index of a key, or -1.
static int64_t aidx (AVal k) {
    if (k.type == A_NUM) {
        double n = k.u.n;
        if (n>=0 && n<4294967296.0 && n==floor(n)) {
            return (int64_t) n;
        }
    }
    return -1;
}

static void apush (ATab* t, AVal v) {
    if (t->an == t->acap) {
        t->acap = (t->acap == 0) ? 4 : t->acap*2;
        t->arr  = realloc(t->arr, t->acap * sizeof(AVal));
    }
    t->arr[t->an++] = v;
}

///////////////////////////////////////////////////////////////////////////////

AVal a_tab_get (ATab* t, AVal k) {
    int64_t i = aidx(k);
    if (i>=0 && i<t->an) {
        return t->arr[i];
    }
    return hget(t, k);
}

void a_tab_set (ATab* t, AVal k, AVal v) {
    int64_t i = aidx(k);
    if (i>=0 && i<t->an) {
        a_cpy(&t->arr[i], v);
        // the last element is never nil
        while (t->an>0 && t->arr[t->an-1].type==A_NIL) {
            t->an--;
        }
    } else if (i>=0 && i==t->an) {
        if (v.type == A_NIL) {
            return;
        }
        a_inc(v);
        apush(t, v);
        // the keys that follow move in from the hash part
        while (t->elive > 0) {
            AVal x = hdel(t, a_num(t->an));
            if (x.type == A_NIL) {
                break;
            }
            apush(t, x);
        }
    } else {
        hset(t, k, v);
    }
}

int a_tab_next (ATab* t, uint32_t* pos, AVal* k, AVal* v) {
    while (*pos < t->an) {
        uint32_t i = (*pos)++;
        if (t->arr[i].type != A_NIL) {
            *k = a_num(i);
            *v = t->arr[i];
            return 1;
        }
    }
    while (*pos - t->an < t->en) {
        AEnt* e = &t->ents[(*pos)++ - t->an];
        if (e->k.type != A_NIL) {
            *k = e->k;
            *v = e->v;
            return 1;
        }
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////

// A task only has the field `pub`.
static int is_pub (AVal t, AVal k) {
    return t.type==A_XTASK && k.type==A_STR && !strcmp(A_STR_OF(k)->s,"pub");
}

AStatus a_index (AVal* dst, AVal t, AVal k) {
    if (is_pub(t, k)) {
        a_cpy(dst, A_TASK_OF(t)->pub);
        return A_OK;
    }
    if (t.type != A_TAB) {
        return a_error("invalid index : expected table : got %s",
            a_typename(t));
    }
    a_cpy(dst, a_tab_get(A_TAB_OF(t), k));
    return A_OK;
}

AStatus a_setindex (AVal t, AVal k, AVal v) {
    if (is_pub(t, k)) {
        a_cpy(&A_TASK_OF(t)->pub, v);
        return A_OK;
    }
    if (t.type != A_TAB) {
        return a_error("invalid index : expected table : got %s",
            a_typename(t));
    }
    if (k.type==A_NIL || (k.type==A_NUM && k.u.n!=k.u.n)) {
        return a_error("invalid index : unexpected %s",
            (k.type == A_NIL) ? "nil" : "nan");
    }
    a_tab_set(A_TAB_OF(t), k, v);
    return A_OK;
}

AStatus a_len (AVal* dst, AVal v) {
    switch (v.type) {
        case A_TAB:
            a_set(dst, a_num(A_TAB_OF(v)->an));
            return A_OK;
        case A_STR:
            a_set(dst, a_num(A_STR_OF(v)->len));
            return A_OK;
        case A_TASKS:
            a_set(dst, a_num(a_tasks_len(A_TASKS_OF(v))));
            return A_OK;
        default:
            return a_error("invalid operation '#' : unexpected %s",
                a_typename(v));
    }
}

static AStatus check (AVal t) {
    if (t.type != A_TAB) {
        return a_error("invalid index : expected table : got %s",
            a_typename(t));
    }
    return A_OK;
}

AStatus a_index_last (AVal* dst, AVal t) {
    if (check(t) != A_OK) {
        return A_ERROR;
    }
    return a_index(dst, t, a_num((double)A_TAB_OF(t)->an - 1));
}

AStatus a_setindex_last (AVal t, AVal v) {
    if (check(t) != A_OK) {
        return A_ERROR;
    }
    return a_setindex(t, a_num((double)A_TAB_OF(t)->an - 1), v);
}

AStatus a_setindex_next (AVal t, AVal v) {
    if (check(t) != A_OK) {
        return A_ERROR;
    }
    return a_setindex(t, a_num(A_TAB_OF(t)->an), v);
}

AStatus a_tag (AVal t, AVal tag) {
    if (t.type != A_TAB) {
        return a_error("invalid tag operation : expected table : got %s",
            a_typename(t));
    }
    AVal k = a_strz("tag");
    a_tab_set(A_TAB_OF(t), k, tag);
    a_dec(k);
    return A_OK;
}

void a_tab_fill (AVal t, int i, int n) {
    for (int j=0; j<n; j++) {
        a_tab_set(A_TAB_OF(t), a_num(i+j), a_stk[a_top-n+j]);
    }
    a_settop(a_top - n);
}

int a_tab_unpack (AVal t) {
    ATab* x = A_TAB_OF(t);
    for (uint32_t i=0; i<x->an; i++) {
        a_pushc(x->arr[i]);
    }
    return x->an;
}
