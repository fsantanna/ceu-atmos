// X.load: reads a value written with the constructors of the language.
//
//      Value : `nil´ | `true´ | `false´
//            | [`-´] NUM | STR | TAG
//            | [TAG] `[´ { Entry `,´ } `]´
//      Entry : ID `=´ Value
//            | `@´ Key `=´ Value
//            | Value
//      Key   : `(´ Value `)´ | NUM | STR | TAG
//
// It is data only: there are no variables, operations, functions, or
// calls. Comments are accepted: `;; ...` up to the end of the line,
// and `;;; ... ;;;` over multiple lines.

#include <ctype.h>
#include <stdio.h>

#include "lib.h"

// Nested tables allowed, to protect the C stack.
#define DEPTH_MAX 200

typedef struct Load {
    const char* s;
    const char* end;
    int lin;
    int depth;
} Load;

static AStatus fail (Load* l, const char* msg) {
    return a_error("invalid load : line %d : %s", l->lin, msg);
}

// Skips blanks and comments. Returns 0 on an unterminated comment.
static int skip (Load* l) {
    while (l->s < l->end) {
        char c = *l->s;
        if (c == '\n') {
            l->lin++;
            l->s++;
        } else if (c==' ' || c=='\t' || c=='\r') {
            l->s++;
        } else if (c==';' && l->s+1<l->end && l->s[1]==';') {
            // the number of semicolons tells the kind
            int n = 0;
            while (l->s<l->end && *l->s==';') {
                n++;
                l->s++;
            }
            if (n == 2) {
                while (l->s<l->end && *l->s!='\n') {
                    l->s++;
                }
                continue;
            }
            // up to the same number of semicolons
            int m = 0;
            while (l->s<l->end && m!=n) {
                if (*l->s == ';') {
                    m++;
                    if (m==n && l->s+1<l->end && l->s[1]==';') {
                        // a longer run does not close it
                        while (l->s<l->end && *l->s==';') {
                            l->s++;
                        }
                        m = 0;
                        continue;
                    }
                } else {
                    m = 0;
                    if (*l->s == '\n') {
                        l->lin++;
                    }
                }
                l->s++;
            }
            if (m != n) {
                return 0;
            }
        } else {
            break;
        }
    }
    return 1;
}

static int is_id1 (char c) {
    return isalpha((unsigned char)c) || c=='_';
}

static int is_id (char c) {
    return isalnum((unsigned char)c) || c=='_';
}

// The next token is the symbol `c`: consumes it.
static int accept (Load* l, char c) {
    if (l->s<l->end && *l->s==c) {
        l->s++;
        return 1;
    }
    return 0;
}

static AStatus value (Load* l, AVal* dst);

// NUM: as a literal of the language, plus an exponent with a sign.
static AStatus number (Load* l, int neg, AVal* dst) {
    const char* s = l->s;
    const char* p = s;
    int hex = (p+1<l->end && p[0]=='0' && (p[1]=='x' || p[1]=='X'));
    int real = 0;
    while (p < l->end) {
        char c = *p;
        if (isalnum((unsigned char)c) || c=='.') {
            real = real || (c == '.') ||
                   (!hex && (c=='e' || c=='E')) ||
                   ( hex && (c=='p' || c=='P'));
            p++;
        } else if ((c=='+' || c=='-') && p>s &&
                   ((!hex && (p[-1]=='e' || p[-1]=='E')) ||
                    ( hex && (p[-1]=='p' || p[-1]=='P'))))
        {
            p++;
        } else {
            break;
        }
    }
    char buf[64];
    size_t n = p - s;
    if (n >= sizeof(buf)) {
        return fail(l, "invalid number");
    }
    memcpy(buf, s, n);
    buf[n] = '\0';

    char* stop;
    if (!real) {
        unsigned long long v = strtoull(buf, &stop, 0);
        if (*stop != '\0') {
            return fail(l, "invalid number");
        }
        if (hex || (n<=19 && v<=9223372036854775807ULL)) {
            a_set(dst, a_int(neg ? (int64_t)(0 - v) : (int64_t)v));
            l->s = p;
            return A_OK;
        }
    }
    double d = strtod(buf, &stop);
    if (*stop != '\0') {
        return fail(l, "invalid number");
    }
    a_set(dst, a_num(neg ? -d : d));
    l->s = p;
    return A_OK;
}

// STR: one delimiter is a single line with escapes, three or more are
// multiple lines as they are, and two are the empty string.
static AStatus string (Load* l, AVal* dst) {
    char q = *l->s;
    int n = 0;
    while (l->s<l->end && *l->s==q) {
        n++;
        l->s++;
    }
    if (n == 2) {
        a_set(dst, a_strz(""));
        return A_OK;
    }

    Buf b = { NULL, 0, 0 };
    if (n > 1) {
        const char* s = l->s;
        int m = 0;
        while (l->s<l->end && m!=n) {
            m = (*l->s == q) ? m+1 : 0;
            if (*l->s == '\n') {
                l->lin++;
            }
            l->s++;
        }
        if (m != n) {
            return fail(l, "unterminated string");
        }
        a_set(dst, a_str(s, (l->s - n) - s));
        return A_OK;
    }

    while (1) {
        if (l->s>=l->end || *l->s=='\n') {
            free(b.s);
            return fail(l, "unterminated string");
        }
        char c = *l->s++;
        if (c == q) {
            break;
        }
        if (c != '\\') {
            buf_add(&b, &c, 1);
            continue;
        }
        if (l->s >= l->end) {
            free(b.s);
            return fail(l, "unterminated string");
        }
        c = *l->s++;
        int v = -1;
        switch (c) {
            case 'a':  v = '\a'; break;
            case 'b':  v = '\b'; break;
            case 'f':  v = '\f'; break;
            case 'n':  v = '\n'; break;
            case 'r':  v = '\r'; break;
            case 't':  v = '\t'; break;
            case 'v':  v = '\v'; break;
            case '\\': v = '\\'; break;
            case '"':  v = '"';  break;
            case '\'': v = '\''; break;
            case 'x':
                // \xHH
                if (l->s+1<l->end && isxdigit((unsigned char)l->s[0]) &&
                    isxdigit((unsigned char)l->s[1]))
                {
                    char hh[3] = { l->s[0], l->s[1], '\0' };
                    v = (int) strtol(hh, NULL, 16);
                    l->s += 2;
                }
                break;
            default:
                // \ddd
                if (isdigit((unsigned char)c)) {
                    v = c - '0';
                    for (int i=0; i<2 && l->s<l->end &&
                                  isdigit((unsigned char)*l->s); i++) {
                        v = v*10 + (*l->s++ - '0');
                    }
                    if (v > 255) {
                        v = -1;
                    }
                }
                break;
        }
        if (v < 0) {
            free(b.s);
            return fail(l, "invalid escape sequence");
        }
        char ch = (char) v;
        buf_add(&b, &ch, 1);
    }
    a_set(dst, buf_val(&b));
    return A_OK;
}

// TAG: the string after the colon.
static AStatus tag (Load* l, AVal* dst) {
    const char* s = ++l->s;
    while (l->s<l->end && (is_id(*l->s) || *l->s=='.')) {
        l->s++;
    }
    if (l->s == s) {
        return fail(l, "invalid tag");
    }
    a_set(dst, a_str(s, l->s - s));
    return A_OK;
}

// After `[`: the entries, into `t`.
static AStatus table (Load* l, AVal t) {
    if (++l->depth > DEPTH_MAX) {
        return fail(l, "too many nested tables");
    }
    int64_t n = 0;
    while (1) {
        if (!skip(l)) {
            return fail(l, "unterminated comment");
        }
        if (accept(l, ']')) {
            break;
        }

        AVal k = a_nil();
        AVal v = a_nil();
        AStatus st = A_OK;
        int key = 0;
        if (accept(l, '@')) {
            // @(v)=  @num=  @"s"=  @:tag=
            key = 1;
            if (accept(l, '(')) {
                st = value(l, &k);
                if (st==A_OK && !(skip(l) && accept(l,')'))) {
                    st = fail(l, "expected ')'");
                }
            } else if (l->s<l->end &&
                       (isdigit((unsigned char)*l->s) || *l->s=='"' ||
                        *l->s=='\'' || *l->s==':'))
            {
                st = value(l, &k);
            } else {
                st = fail(l, "expected key");
            }
        } else if (l->s<l->end && is_id1(*l->s)) {
            // id=  (or nil, true, false as a value)
            const char* p = l->s;
            while (p<l->end && is_id(*p)) {
                p++;
            }
            const char* q = p;
            while (q<l->end && (*q==' ' || *q=='\t')) {
                q++;
            }
            if (q<l->end && *q=='=') {
                key = 1;
                k = a_str(l->s, p - l->s);
                l->s = p;
            }
        }
        if (st==A_OK && key) {
            if (!(skip(l) && accept(l,'='))) {
                st = fail(l, "expected '='");
            } else if (k.type==A_NIL || (k.type==A_NUM && k.u.n!=k.u.n)) {
                st = fail(l, "invalid key");
            }
        }
        if (st == A_OK) {
            st = value(l, &v);
        }
        if (st == A_OK) {
            a_tab_set(A_TAB_OF(t), key ? k : a_int(n), v);
            n += !key;
        }
        a_dec(k);
        a_dec(v);
        if (st != A_OK) {
            return st;
        }

        if (!skip(l)) {
            return fail(l, "unterminated comment");
        }
        if (accept(l, ',')) {
            continue;
        }
        if (accept(l, ']')) {
            break;
        }
        return fail(l, "expected ',' or ']'");
    }
    l->depth--;
    return A_OK;
}

static AStatus value (Load* l, AVal* dst) {
    if (!skip(l)) {
        return fail(l, "unterminated comment");
    }
    if (l->s >= l->end) {
        return fail(l, "expected value");
    }
    char c = *l->s;

    if (c == '[') {
        l->s++;
        AVal t = a_tab();
        AStatus st = table(l, t);
        if (st == A_OK) {
            a_set(dst, t);
        } else {
            a_dec(t);
        }
        return st;
    }
    if (c == ':') {
        AVal tg = a_nil();
        if (tag(l, &tg) != A_OK) {
            return A_ERROR;
        }
        // :X [...]: in the same line
        const char* p = l->s;
        while (p<l->end && (*p==' ' || *p=='\t')) {
            p++;
        }
        if (p>=l->end || *p!='[') {
            a_set(dst, tg);
            return A_OK;
        }
        l->s = p + 1;
        AVal t = a_tab();
        AVal k = a_strz("tag");
        a_tab_set(A_TAB_OF(t), k, tg);
        a_dec(k);
        a_dec(tg);
        AStatus st = table(l, t);
        if (st == A_OK) {
            a_set(dst, t);
        } else {
            a_dec(t);
        }
        return st;
    }
    if (c=='"' || c=='\'') {
        return string(l, dst);
    }
    if (isdigit((unsigned char)c)) {
        return number(l, 0, dst);
    }
    if (c=='-' && l->s+1<l->end && isdigit((unsigned char)l->s[1])) {
        l->s++;
        return number(l, 1, dst);
    }
    if (is_id1(c)) {
        const char* p = l->s;
        while (p<l->end && is_id(*p)) {
            p++;
        }
        size_t n = p - l->s;
        if (n==3 && !strncmp(l->s,"nil",3)) {
            a_set(dst, a_nil());
        } else if (n==4 && !strncmp(l->s,"true",4)) {
            a_set(dst, a_bool(1));
        } else if (n==5 && !strncmp(l->s,"false",5)) {
            a_set(dst, a_bool(0));
        } else {
            return fail(l, "unexpected name : expected value");
        }
        l->s = p;
        return A_OK;
    }
    return fail(l, "expected value");
}

// Reads the single value in `s[0..len)`.
static AStatus load (const char* s, size_t len) {
    Load l = { s, s+len, 1, 0 };
    AVal v = a_nil();
    AStatus st = value(&l, &v);
    if (st==A_OK && !skip(&l)) {
        st = fail(&l, "unterminated comment");
    }
    if (st==A_OK && l.s<l.end) {
        st = fail(&l, "expected end of input");
    }
    if (st == A_OK) {
        a_push(v);
    } else {
        a_dec(v);
    }
    return st;
}

// X.load(s): the value written in the string `s`.
static AStatus x_load (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AStr* s;
    if (arg_str(argc, argv, 0, "load", &s) != A_OK) {
        return A_ERROR;
    }
    return load(s->s, s->len);
}

// X.loadfile(path): the value written in a file.
static AStatus x_loadfile (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AStr* path;
    if (arg_str(argc, argv, 0, "loadfile", &path) != A_OK) {
        return A_ERROR;
    }
    FILE* f = fopen(path->s, "rb");
    if (f == NULL) {
        return a_error("invalid load : cannot read file : %s", path->s);
    }
    Buf b = { NULL, 0, 0 };
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        buf_add(&b, chunk, n);
    }
    fclose(f);
    AStatus st = load(b.s ? b.s : "", b.n);
    free(b.s);
    return st;
}

void atm_lib_load_init (void) {
    field(atm_g_X, "load",     x_load);
    field(atm_g_X, "loadfile", x_loadfile);
}
