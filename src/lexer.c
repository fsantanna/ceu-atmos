// Atmos compiler: lexer.
//
// Symbols, operators, tags, keywords, identifiers, numbers, clocks,
// strings, natives, and comments.

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "atmc.h"

static const char* SRC;
static size_t      LEN;
static size_t      I;
static int         LIN;
static int         SEP;
static int         END;

static const char* SYMS = "{}()[],\\@";

static const char* OPCS = "+-*/%><=|&?!#~^";

static const char* OPS[] = {
    "#",
    "=", "=>",
    "==", "!=",
    ">", "<", ">=", "<=",
    "||", "&&",
    "+", "-", "*", "/", "//", "%",
    "!",
    "++",
    "&", "|", "^", "~", "<<", ">>",
    "??", "!?",
    "->", "-->", "<-", "<--",
    NULL
};

static const char* KEYS[] = {
    "await", "catch", "defer", "do", "else", "emit", "false", "func", "if",
    "ifs", "in", "loop", "match", "nil", "on", "par", "pin", "set", "spawn",
    "task", "tasks", "test", "thread", "toggle", "true", "until", "val",
    "var", "watching", "where", "with", "while",
    NULL
};

static const char* TAGS[] = {
    "sym", "op", "tag", "key", "id", "num", "clk", "nat", "str", "eof"
};

const char* lexer_tag (TkTag tag) {
    return TAGS[tag];
}

///////////////////////////////////////////////////////////////////////////////

static int contains (const char** set, const char* s, size_t n) {
    for (int i=0; set[i]!=NULL; i++) {
        if (strlen(set[i])==n && !strncmp(set[i],s,n)) {
            return 1;
        }
    }
    return 0;
}

// The source ends with an extra '\0', which yields the eof token.

static int read (void) {
    int c = (unsigned char) SRC[I];
    if (c == '\n') {
        LIN++;
        SEP++;
    } else if (c == ';') {
        SEP++;
    }
    I++;
    return c;
}

static void unread (void) {
    I--;
    int c = (unsigned char) SRC[I];
    if (c == '\n') {
        LIN--;
        SEP--;
    } else if (c == ';') {
        SEP--;
    }
}

static int is_c    (int c, int x) { return c == x; }
static int is_op   (int c, int x) { (void)x; return c && strchr(OPCS,c); }
static int is_tag  (int c, int x) { (void)x; return isalnum(c) || c=='_' || c=='.'; }
static int is_id   (int c, int x) { (void)x; return isalnum(c) || c=='_'; }
static int is_num  (int c, int x) { (void)x; return isalnum(c) || c=='.'; }
static int is_exp  (int c, int x) { (void)x; return isalnum(c) || c=='.' || c=='-' || c=='+'; }
static int is_eol  (int c, int x) { (void)x; return c=='\n' || c=='\0'; }
static int is_quo  (int c, int x) { return c=='\n' || c==x; }

// Consumes while `f` holds (or until, if `neg`).
// Returns the number of characters, or -1 at the end of the source.
static int read_while (int (*f)(int,int), int x, int neg) {
    int n = 0;
    while (1) {
        int c = read();
        int ok = f(c, x);
        if (neg ? ok : !ok) {
            break;
        }
        if (c == '\0') {
            return -1;
        }
        n++;
    }
    unread();
    return n;
}

static Tk mk (TkTag tag, const char* str, int len, int lin, int sep) {
    Tk tk;
    memset(&tk, 0, sizeof(tk));
    tk.tag = tag;
    tk.str = xstrndup(str, len);
    tk.len = len;
    tk.lin = lin;
    tk.sep = sep;
    return tk;
}

///////////////////////////////////////////////////////////////////////////////

// ;; line comment
// ;;; multi-line comment ;;; (nests with longer delimiters)
static void comment (void) {
    size_t i0 = I - 1;
    int n = 1 + read_while(is_c, ';', 0);
    if (n == 1) {
        // single ';': separator only
        return;
    }
    if (n == 2) {
        read_while(is_eol, 0, 1);
        return;
    }

    int lin = LIN;
    int sep = SEP;
    int stk[64];
    int top = 0;
    while (1) {
        if (top>0 && stk[top-1]==n) {
            top--;
            if (top == 0) {
                break;
            }
        } else if (top < 64) {
            stk[top++] = n;
        }
        do {
            if (read_while(is_c, ';', 1) < 0) {
                err(mk(TK_SYM,SRC+i0,stk[0],lin,sep), "unterminated comment");
            }
            n = read_while(is_c, ';', 0);
        } while (!(n>2 && n>=stk[top-1]));
    }
}

// Clock: digits+unit in descending order: day h min s ms us.
// Returns the duration in microseconds, or -1 if malformed.
static double clock (const char* s) {
    static const char*  US[] = { "day", "h", "min", "s", "ms", "us" };
    static const double VS[] = { 864e8, 36e8, 6e7, 1e6, 1e3, 1 };
    double ret = 0;
    int ok = 0;
    for (int u=0; u<6; u++) {
        const char* p = s;
        while (isdigit((unsigned char)*p)) {
            p++;
        }
        if (p == s) {
            continue;
        }
        if (*p == '.') {
            p++;
        }
        while (isdigit((unsigned char)*p)) {
            p++;
        }
        size_t n = strlen(US[u]);
        if (strncmp(p, US[u], n) != 0) {
            continue;
        }
        // "s" must not shadow a longer unit
        if (isalpha((unsigned char)p[n])) {
            continue;
        }
        ret += strtod(s, NULL) * VS[u];
        ok = 1;
        s = p + n;
    }
    return (ok && *s=='\0') ? ret : -1;
}

// Strings '...' "..." and natives `...`: a single delimiter is
// single-line, three or more are multi-line.
static Tk quoted (int c) {
    int lin = LIN;
    int sep = SEP;
    TkTag tag = (c == '`') ? TK_NAT : TK_STR;
    const char* what = (c == '`') ? "native" : "string";

    size_t i0 = I - 1;
    int n1 = 1 + read_while(is_c, c, 0);
    size_t v0 = I;
    size_t v1 = I;

    if (n1 == 2) {
        // empty
    } else if (n1 == 1) {
        if (read_while(is_quo, c, 1) < 0) {
            err(mk(tag,SRC+i0,1,lin,sep), "unterminated %s", what);
        }
        if (SRC[I] != c) {
            err(mk(tag,SRC+I-1,1,lin,sep), "unterminated %s", what);
        }
        v1 = I;
        read();
    } else {
        while (1) {
            if (read_while(is_c, c, 1) < 0) {
                err(mk(tag,SRC+i0,n1,lin,sep), "unterminated %s", what);
            }
            v1 = I;
            int n2 = read_while(is_c, c, 0);
            if (n1 == n2) {
                break;
            }
        }
    }

    Tk tk = mk(tag, SRC+v0, v1-v0, lin, sep);
    tk.chr   = c;
    tk.multi = (n1 >= 3);
    return tk;
}

///////////////////////////////////////////////////////////////////////////////

void lexer_init (const char* file, const char* src, size_t len) {
    FILENAME = file;
    SRC = src;
    LEN = len;
    I   = 0;
    LIN = 1;
    SEP = 1;
    END = 0;
}

int lexer_token (Tk* tk) {
    while (!END && I<=LEN) {
        size_t i0 = I;
        int c = read();

        // spaces
        if (c!='\0' && isspace(c)) {
            continue;

        // comments
        } else if (c == ';') {
            comment();
            continue;

        // symbols:  {  (  ,  @  [  ]
        } else if (c!='\0' && strchr(SYMS,c)) {
            *tk = mk(TK_SYM, SRC+i0, 1, LIN, SEP);
            return 1;

        // symbols:  .  ...
        } else if (c == '.') {
            if (SRC[I]=='.' && SRC[I+1]=='.') {
                read();
                read();
                *tk = mk(TK_SYM, "...", 3, LIN, SEP);
            } else {
                *tk = mk(TK_SYM, ".", 1, LIN, SEP);
            }
            return 1;

        // operators:  +  >=  #
        } else if (is_op(c,0)) {
            int n = 1 + read_while(is_op, 0, 0);
            int m = n;
            while (!contains(OPS, SRC+i0, m)) {
                if (m == 0) {
                    err(mk(TK_OP,SRC+i0,n,LIN,SEP), "invalid operator");
                }
                unread();
                m--;
            }
            *tk = mk(TK_OP, SRC+i0, m, LIN, SEP);
            return 1;

        // tags:  :X  :a.b.c
        } else if (c == ':') {
            if (SRC[I] == ':') {
                read();
                *tk = mk(TK_SYM, "::", 2, LIN, SEP);
            } else {
                int n = 1 + read_while(is_tag, 0, 0);
                *tk = mk(TK_TAG, SRC+i0, n, LIN, SEP);
            }
            return 1;

        // keywords:  await  if
        // variables:  x  a_10
        } else if (isalpha(c) || c=='_') {
            int n = 1 + read_while(is_id, 0, 0);
            TkTag tag = contains(KEYS, SRC+i0, n) ? TK_KEY : TK_ID;
            *tk = mk(tag, SRC+i0, n, LIN, SEP);
            return 1;

        // numbers:  0xFF  10.1
        // clocks:   5s  300ms  2h30min
        } else if (isdigit(c)) {
            int n = 1 + read_while(is_num, 0, 0);
            if (memchr(SRC+i0,'e',n) || memchr(SRC+i0,'E',n) ||
                memchr(SRC+i0,'p',n) || memchr(SRC+i0,'P',n))
            {
                n += read_while(is_exp, 0, 0);
            }
            *tk = mk(TK_NUM, SRC+i0, n, LIN, SEP);
            char* end;
            strtod(tk->str, &end);
            if (*end != '\0') {
                tk->clk = clock(tk->str);
                if (tk->clk < 0) {
                    err(*tk, "invalid number");
                }
                tk->tag = TK_CLK;
            }
            return 1;

        // natives:  `...`
        // strings:  '...'  "..."
        } else if (c=='`' || c=='"' || c=='\'') {
            *tk = quoted(c);
            return 1;

        // eof
        } else if (c == '\0') {
            *tk = mk(TK_EOF, "<eof>", 5, LIN, SEP);
            END = 1;
            return 1;

        // error
        } else {
            err(mk(TK_SYM,SRC+i0,1,LIN,SEP), "invalid character");
        }
    }
    return 0;
}
