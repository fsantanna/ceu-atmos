// Atmos compiler: command line.
//
//   atmos f.atm [args]     compiles, builds, and runs
//   atmos -c f.atm         emits C only (to stdout)
//   atmos -o out f.atm     builds only
//   atmos -t f.atm         enables `test` blocks
//
// Environment:
//   ATMOS_HOME     root with `rt/` and `build/` (default: build dir)
//   ATMOS_CC       C compiler (default: cc)
//   ATMOS_CFLAGS   C flags (default: -O1)
//   ATMOS_ASAN     if set, builds with the address sanitizer

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "atmc.h"

#ifndef ATMOS_HOME
#define ATMOS_HOME "."
#endif

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

static const char* env (const char* name, const char* def) {
    const char* v = getenv(name);
    return (v!=NULL && v[0]!='\0') ? v : def;
}

// Builds the C source into an executable. Returns 0 on success.
static int build (const char* c, const char* exe) {
    const char* home = env("ATMOS_HOME", ATMOS_HOME);
    int asan = (getenv("ATMOS_ASAN") != NULL);
    char* cmd = xfmt(
        "%s %s %s -w -I%s/rt %s %s/build/libatmos%s.a -lm -o %s",
        env("ATMOS_CC", "cc"),
        env("ATMOS_CFLAGS", "-O1"),
        asan ? "-fsanitize=address -fno-omit-frame-pointer -g" : "",
        home, c, home,
        asan ? "-asan" : "",
        exe
    );
    return system(cmd);
}

static void usage (void) {
    fprintf(stderr,
        "Usage: atmos [-c] [-o <out>] [-t] <input> [<args>] ...\n"
        "\n"
        "Options:\n"
        "   -c          Emit C only.\n"
        "   -o <out>    Build only.\n"
        "   -t          Enable test blocks.\n"
    );
    exit(1);
}

int main (int argc, char** argv) {
    int lex  = 0;
    int ast  = 0;
    int only = 0;
    const char* exe = NULL;
    const char* inp = NULL;
    int i = 1;
    for (; i<argc && inp==NULL; i++) {
        if (!strcmp(argv[i], "--lex")) {
            lex = 1;
        } else if (!strcmp(argv[i], "--ast")) {
            ast = 1;
        } else if (!strcmp(argv[i], "-c")) {
            only = 1;
        } else if (!strcmp(argv[i], "-t")) {
            OPT_TEST = 1;
        } else if (!strcmp(argv[i], "-o") && i+1<argc) {
            exe = argv[++i];
        } else if (argv[i][0] == '-') {
            usage();
        } else {
            inp = argv[i];
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

    Expr* prog = parser_main();

    if (ast) {
        for (int j=0; j<prog->u.block.es.n; j++) {
            ast_dump(prog->u.block.es.v[j]);
            printf("\n");
        }
        return 0;
    }

    char* c = coder_main(prog);

    if (only) {
        fputs(c, stdout);
        return 0;
    }

    // builds in a temporary directory, removed at the end
    char dir[] = "/tmp/atmos-XXXXXX";
    if (mkdtemp(dir) == NULL) {
        fprintf(stderr, "atmos : cannot create temporary directory\n");
        return 1;
    }
    char* cfile = xfmt("%s/main.c", dir);
    char* xfile = (exe != NULL) ? (char*)exe : xfmt("%s/main", dir);

    FILE* f = fopen(cfile, "w");
    fputs(c, f);
    fclose(f);

    int ret = build(cfile, xfile);
    if (ret != 0) {
        fprintf(stderr, "atmos : C compilation failed\n");
        ret = 1;
    } else if (exe == NULL) {
        // runs the program with the remaining arguments
        pid_t pid = fork();
        if (pid == 0) {
            argv[i-1] = xfile;
            execv(xfile, &argv[i-1]);
            _exit(127);
        }
        int status = 0;
        waitpid(pid, &status, 0);
        ret = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        unlink(xfile);
    }

    unlink(cfile);
    rmdir(dir);
    return ret;
}
