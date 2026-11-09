// Interned strings.

#include <stdlib.h>
#include <string.h>

#include "atmos.h"

// Weak intern table: open addressing with linear probing. A string
// leaves the table when its last reference is released.

static AStr** TAB = NULL;
static size_t CAP = 0;
static size_t CNT = 0;

static uint32_t hash (const char* s, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i=0; i<len; i++) {
        h = (h ^ (unsigned char)s[i]) * 16777619u;
    }
    return h;
}

static void insert (AStr* s) {
    size_t i = s->hash & (CAP - 1);
    while (TAB[i] != NULL) {
        i = (i + 1) & (CAP - 1);
    }
    TAB[i] = s;
}

static void grow (void) {
    AStr** old = TAB;
    size_t cap = CAP;
    CAP = (CAP == 0) ? 64 : CAP*2;
    TAB = calloc(CAP, sizeof(AStr*));
    for (size_t i=0; i<cap; i++) {
        if (old[i] != NULL) {
            insert(old[i]);
        }
    }
    free(old);
}

AVal a_str (const char* s, size_t len) {
    if ((CNT+1)*2 > CAP) {
        grow();
    }
    uint32_t h = hash(s, len);
    size_t i = h & (CAP - 1);
    while (TAB[i] != NULL) {
        AStr* x = TAB[i];
        if (x->hash==h && x->len==len && !memcmp(x->s,s,len)) {
            x->hdr.rc++;
            return a_obj(&x->hdr);
        }
        i = (i + 1) & (CAP - 1);
    }

    AStr* x = malloc(sizeof(AStr) + len + 1);
    x->hdr.rc     = 1;
    x->hdr.type   = A_STR;
    x->hdr.pinned = 0;
    x->hdr.depth  = 0;
    x->hash = h;
    x->len  = len;
    memcpy(x->s, s, len);
    x->s[len] = '\0';
    TAB[i] = x;
    CNT++;
    return a_obj(&x->hdr);
}

AVal a_strz (const char* s) {
    return a_str(s, strlen(s));
}

// Removes the string from the table, shifting back the entries that
// follow it in the same probe sequence.
void a_str_free (AStr* s) {
    size_t i = s->hash & (CAP - 1);
    while (TAB[i] != s) {
        i = (i + 1) & (CAP - 1);
    }
    size_t j = i;
    while (1) {
        j = (j + 1) & (CAP - 1);
        if (TAB[j] == NULL) {
            break;
        }
        size_t k = TAB[j]->hash & (CAP - 1);
        int between = (i <= j) ? (i<k && k<=j) : (i<k || k<=j);
        if (!between) {
            TAB[i] = TAB[j];
            i = j;
        }
    }
    TAB[i] = NULL;
    CNT--;
}

void a_str_quit (void) {
    free(TAB);
    TAB = NULL;
    CAP = 0;
    CNT = 0;
}
