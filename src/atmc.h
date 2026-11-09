// Atmos compiler: shared declarations.

#ifndef ATMC_H
#define ATMC_H

#include <stddef.h>

///////////////////////////////////////////////////////////////////////////////
// AUX
///////////////////////////////////////////////////////////////////////////////

// Allocations live until the compiler exits.
void* xalloc (size_t n);
char* xstrndup (const char* s, size_t n);
char* xfmt (const char* fmt, ...);

///////////////////////////////////////////////////////////////////////////////
// LEXER
///////////////////////////////////////////////////////////////////////////////

typedef enum {
    TK_SYM, TK_OP, TK_TAG, TK_KEY, TK_ID,
    TK_NUM, TK_CLK, TK_NAT, TK_STR, TK_EOF
} TkTag;

typedef struct Tk {
    TkTag       tag;
    const char* str;
    int         len;
    // line, and separator counter (newlines and semicolons)
    int         lin;
    int         sep;
    // strings: delimiter and multi-line flag
    char        chr;
    int         multi;
    // clocks: duration in microseconds
    double      clk;
} Tk;

extern const char* FILENAME;

// Reports "FILE : line N : near 'STR' : MSG" and exits.
void err (Tk tk, const char* fmt, ...);

void lexer_init (const char* file, const char* src, size_t len);

// Next token. Returns 0 after the end of file was delivered.
int lexer_token (Tk* tk);

const char* lexer_tag (TkTag tag);

#endif
