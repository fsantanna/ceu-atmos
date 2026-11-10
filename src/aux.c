// Atmos compiler: allocation and error helpers.

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atmc.h"

const char* FILENAME = "?";

void* xalloc (size_t n) {
    void* p = calloc(1, n ? n : 1);
    if (p == NULL) {
        fprintf(stderr, "atmos : out of memory\n");
        exit(1);
    }
    return p;
}

char* xstrndup (const char* s, size_t n) {
    char* p = xalloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char* xfmt (const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    char* p = xalloc(n + 1);
    va_start(ap, fmt);
    vsnprintf(p, n + 1, fmt, ap);
    va_end(ap);
    return p;
}

void err (Tk tk, const char* fmt, ...) {
    va_list ap;
    fflush(stdout);
    fprintf(stderr, "%s : line %d : near '%s' : ", FILENAME, tk.lin, tk.str);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

NatKind nat_kind (const char* s, const char** code) {
    // in the order of `NatKind`
    static const char* TAGS[] = {
        "", "pre", "number.integer", "number.float", "boolean", "string",
        "val", "number", NULL
    };
    while (*s==' ' || *s=='\t' || *s=='\n') {
        s++;
    }
    *code = s;
    if (*s != ':') {
        return NAT_STMT;
    }
    // a tag may have dots: :number.float
    int n = 0;
    while (nat_id(s+1+n)>0 || (n>0 && s[1+n]=='.' && nat_id(s+2+n)>0)) {
        n += (s[1+n] == '.') ? 1 : nat_id(s+1+n);
    }
    for (int i=1; TAGS[i]!=NULL; i++) {
        if ((int)strlen(TAGS[i])==n && !strncmp(s+1,TAGS[i],n)) {
            *code = s + 1 + n;
            return (NatKind) i;
        }
    }
    return NAT_ERR;
}

int nat_id (const char* s) {
    int n = 0;
    while ((s[n]>='a' && s[n]<='z') || (s[n]>='A' && s[n]<='Z') ||
           s[n]=='_' || (n>0 && s[n]>='0' && s[n]<='9'))
    {
        n++;
    }
    return n;
}
