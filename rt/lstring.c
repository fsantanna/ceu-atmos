// Library `string`. Positions start at 0, and a negative position
// counts from the end: -1 is the last byte.

#include <ctype.h>
#include <stdio.h>

#include "lib.h"

AVal atm_g_string;

// Position `i` in a string of length `len`, counted from the end if
// negative.
static int64_t pos (int64_t i, int64_t len) {
    return (i < 0) ? i + len : i;
}

// string.len(s)
static AStatus str_len (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AStr* s;
    if (arg_str(argc, argv, 0, "len", &s) != A_OK) {
        return A_ERROR;
    }
    a_push(a_int(s->len));
    return A_OK;
}

// string.sub(s, i, j): bytes `i` to `j`, both included. They default
// to the first and last.
static AStatus str_sub (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AStr* s;
    int64_t i, j;
    if (arg_str(argc,argv,0,"sub",&s)!=A_OK ||
        arg_int(argc,argv,1,"sub",1,0,&i)!=A_OK ||
        arg_int(argc,argv,2,"sub",1,-1,&j)!=A_OK)
    {
        return A_ERROR;
    }
    i = pos(i, s->len);
    j = pos(j, s->len);
    if (i < 0) {
        i = 0;
    }
    if (j >= s->len) {
        j = (int64_t)s->len - 1;
    }
    a_push((i > j) ? a_strz("") : a_str(s->s+i, j-i+1));
    return A_OK;
}

// string.find(s, x, i): first and last positions of the first
// occurrence of the plain string `x` in `s`, from position `i` on.
// Gives nil if there is none.
static AStatus str_find (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AStr *s, *x;
    int64_t i;
    if (arg_str(argc,argv,0,"find",&s)!=A_OK ||
        arg_str(argc,argv,1,"find",&x)!=A_OK ||
        arg_int(argc,argv,2,"find",1,0,&i)!=A_OK)
    {
        return A_ERROR;
    }
    i = pos(i, s->len);
    if (i < 0) {
        i = 0;
    }
    for (; i+x->len <= s->len; i++) {
        if (memcmp(s->s+i, x->s, x->len) == 0) {
            a_push(a_int(i));
            a_push(a_int(i + x->len - 1));
            return A_OK;
        }
    }
    a_push(a_nil());
    return A_OK;
}

static void split_add (AVal t, const char* s, size_t n) {
    AVal p = a_str(s, n);
    a_tab_set(A_TAB_OF(t), a_int(A_TAB_OF(t)->an), p);
    a_dec(p);
}

// string.split(s, sep): the pieces of `s` between occurrences of the
// plain string `sep`. Without `sep`, the pieces between blanks.
static AStatus str_split (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AStr* s;
    if (arg_str(argc, argv, 0, "split", &s) != A_OK) {
        return A_ERROR;
    }
    AVal ret = a_tab();
    if (arg(argc,argv,1).type == A_NIL) {
        uint32_t i = 0;
        while (i < s->len) {
            while (i<s->len && isspace((unsigned char)s->s[i])) {
                i++;
            }
            uint32_t j = i;
            while (j<s->len && !isspace((unsigned char)s->s[j])) {
                j++;
            }
            if (j > i) {
                split_add(ret, s->s+i, j-i);
            }
            i = j;
        }
        a_push(ret);
        return A_OK;
    }
    AStr* x;
    if (arg_str(argc, argv, 1, "split", &x) != A_OK) {
        a_dec(ret);
        return A_ERROR;
    }
    if (x->len == 0) {
        a_dec(ret);
        return a_error("invalid argument to 'split' : empty separator");
    }
    uint32_t lo = 0;
    uint32_t i  = 0;
    while (i+x->len <= s->len) {
        if (memcmp(s->s+i, x->s, x->len) == 0) {
            split_add(ret, s->s+lo, i-lo);
            i += x->len;
            lo = i;
        } else {
            i++;
        }
    }
    split_add(ret, s->s+lo, s->len-lo);
    a_push(ret);
    return A_OK;
}

// string.byte(s, i, j): the codes of bytes `i` to `j`. They default
// to the first byte only.
static AStatus str_byte (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AStr* s;
    int64_t i, j;
    if (arg_str(argc,argv,0,"byte",&s)!=A_OK ||
        arg_int(argc,argv,1,"byte",1,0,&i)!=A_OK)
    {
        return A_ERROR;
    }
    i = pos(i, s->len);
    if (arg_int(argc,argv,2,"byte",1,i,&j) != A_OK) {
        return A_ERROR;
    }
    j = pos(j, s->len);
    if (i < 0) {
        i = 0;
    }
    if (j >= s->len) {
        j = (int64_t)s->len - 1;
    }
    for (; i<=j; i++) {
        a_push(a_int((unsigned char)s->s[i]));
    }
    return A_OK;
}

// string.char(...): the string with the given byte codes.
static AStatus str_char (AFunc* self, int argc, AVal* argv) {
    (void) self;
    Buf b = { NULL, 0, 0 };
    for (int i=0; i<argc; i++) {
        int64_t c;
        if (arg_int(argc,argv,i,"char",0,0,&c) != A_OK) {
            free(b.s);
            return A_ERROR;
        }
        if (c<0 || c>255) {
            free(b.s);
            return a_error("invalid argument to 'char' : value out of range");
        }
        char ch = (char) c;
        buf_add(&b, &ch, 1);
    }
    a_push(buf_val(&b));
    return A_OK;
}

static AStatus str_case (int up, int argc, AVal* argv) {
    AStr* s;
    if (arg_str(argc, argv, 0, up ? "upper" : "lower", &s) != A_OK) {
        return A_ERROR;
    }
    char* r = malloc(s->len + 1);
    for (uint32_t i=0; i<s->len; i++) {
        unsigned char c = (unsigned char) s->s[i];
        r[i] = up ? toupper(c) : tolower(c);
    }
    a_push(a_str(r, s->len));
    free(r);
    return A_OK;
}

// string.upper(s) / string.lower(s)
static AStatus str_upper (AFunc* self, int argc, AVal* argv) {
    (void) self;
    return str_case(1, argc, argv);
}

static AStatus str_lower (AFunc* self, int argc, AVal* argv) {
    (void) self;
    return str_case(0, argc, argv);
}

// string.rep(s, n, sep): `n` copies of `s`, separated by `sep`.
static AStatus str_rep (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AStr* s;
    AStr* sep = NULL;
    int64_t n;
    if (arg_str(argc,argv,0,"rep",&s)!=A_OK ||
        arg_int(argc,argv,1,"rep",0,0,&n)!=A_OK)
    {
        return A_ERROR;
    }
    if (arg(argc,argv,2).type!=A_NIL &&
        arg_str(argc,argv,2,"rep",&sep)!=A_OK)
    {
        return A_ERROR;
    }
    Buf b = { NULL, 0, 0 };
    for (int64_t i=0; i<n; i++) {
        if (i>0 && sep!=NULL) {
            buf_add(&b, sep->s, sep->len);
        }
        buf_add(&b, s->s, s->len);
    }
    a_push(buf_val(&b));
    return A_OK;
}

// string.format(fmt, ...): as `printf`, with the conversions
// `%d %i %x %X %o %c` (integers), `%f %e %g` (numbers), `%s` (any
// value, as `tostring`), and `%%`. Flags, width, and precision are
// accepted.
static AStatus str_format (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AStr* f;
    if (arg_str(argc, argv, 0, "format", &f) != A_OK) {
        return A_ERROR;
    }
    Buf b = { NULL, 0, 0 };
    int n = 1;
    AStatus st = A_OK;
    const char* p   = f->s;
    const char* end = f->s + f->len;
    while (p<end && st==A_OK) {
        if (*p != '%') {
            buf_add(&b, p++, 1);
            continue;
        }
        p++;
        if (p<end && *p=='%') {
            buf_add(&b, p++, 1);
            continue;
        }

        // %[flags][width][.precision]conv
        char spec[32];
        size_t k = 0;
        spec[k++] = '%';
        while (p<end && strchr("-+ #0123456789.",*p)!=NULL && k<20) {
            spec[k++] = *p++;
        }
        char conv = (p < end) ? *p++ : '\0';
        if (conv=='\0' || strchr("dixXocfeEgGs",conv)==NULL) {
            st = a_error("invalid format : unexpected conversion '%%%c'",
                conv ? conv : ' ');
            break;
        }
        if (n >= argc) {
            st = a_error("invalid format : missing argument");
            break;
        }
        AVal v = argv[n++];

        char  small[128];
        char* out = small;
        int   len = 0;
        if (strchr("dixXoc", conv) != NULL) {
            int64_t i;
            if (!a_toint(v, &i)) {
                st = a_error("invalid format : expected integer : got %s",
                    a_isnum(v) ? "real" : a_typename(v));
                break;
            }
            if (conv == 'c') {
                spec[k++] = 'c';
                spec[k] = '\0';
                len = snprintf(small, sizeof(small), spec, (int)i);
            } else {
                spec[k++] = 'l';
                spec[k++] = 'l';
                spec[k++] = conv;
                spec[k] = '\0';
                len = snprintf(small, sizeof(small), spec, (long long)i);
            }
        } else if (conv == 's') {
            spec[k++] = 's';
            spec[k] = '\0';
            AVal s = a_tostring(v);
            len = snprintf(NULL, 0, spec, A_STR_OF(s)->s);
            out = malloc(len + 1);
            snprintf(out, len+1, spec, A_STR_OF(s)->s);
            a_dec(s);
        } else {
            if (!a_isnum(v)) {
                st = a_error("invalid format : expected number : got %s",
                    a_typename(v));
                break;
            }
            spec[k++] = conv;
            spec[k] = '\0';
            len = snprintf(NULL, 0, spec, a_dbl(v));
            out = malloc(len + 1);
            snprintf(out, len+1, spec, a_dbl(v));
        }
        buf_add(&b, out, (len < 0) ? 0 : (size_t)len);
        if (out != small) {
            free(out);
        }
    }
    if (st != A_OK) {
        free(b.s);
        return st;
    }
    a_push(buf_val(&b));
    return A_OK;
}

void atm_lib_string_init (void) {
    atm_g_string = a_tab();
    field(atm_g_string, "len",    str_len);
    field(atm_g_string, "sub",    str_sub);
    field(atm_g_string, "find",   str_find);
    field(atm_g_string, "split",  str_split);
    field(atm_g_string, "byte",   str_byte);
    field(atm_g_string, "char",   str_char);
    field(atm_g_string, "upper",  str_upper);
    field(atm_g_string, "lower",  str_lower);
    field(atm_g_string, "rep",    str_rep);
    field(atm_g_string, "format", str_format);
}

void atm_lib_string_quit (void) {
    a_clr(&atm_g_string);
}
