// Atmos compiler: command line.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atmc.h"

static char* slurp (const char* path, size_t* len) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "atmos : cannot open '%s'\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = xalloc(n + 1);
    if (fread(buf, 1, n, f) != (size_t)n) {
        fprintf(stderr, "atmos : cannot read '%s'\n", path);
        exit(1);
    }
    fclose(f);
    buf[n] = '\0';
    *len = n;
    return buf;
}

// Dumps one token per line: LIN SEP TAG STR.
static void dump_lex (void) {
    Tk tk;
    while (lexer_token(&tk)) {
        printf("%d %d %s %s", tk.lin, tk.sep, lexer_tag(tk.tag), tk.str);
        if (tk.tag == TK_CLK) {
            printf(" %.0f", tk.clk);
        }
        printf("\n");
    }
}

static void usage (void) {
    fprintf(stderr, "Usage: atmos [--lex] <input>\n");
    exit(1);
}

int main (int argc, char** argv) {
    int lex = 0;
    const char* inp = NULL;
    for (int i=1; i<argc; i++) {
        if (!strcmp(argv[i], "--lex")) {
            lex = 1;
        } else if (inp == NULL) {
            inp = argv[i];
        } else {
            usage();
        }
    }
    if (inp == NULL) {
        usage();
    }

    size_t len;
    char* src = slurp(inp, &len);
    lexer_init(inp, src, len);

    if (lex) {
        dump_lex();
        return 0;
    }

    usage();
    return 0;
}
