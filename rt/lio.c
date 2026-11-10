// Library `io`. A file is a native box that closes itself when it is
// released, or when its pin block ends.

#include <errno.h>
#include <stdio.h>

#include "lib.h"

AVal atm_g_io;

static const char* FILE_KIND = "file";

static void file_fin (void* ptr) {
    fclose((FILE*) ptr);
}

// Argument `i` of function `f` as an open file.
static AStatus arg_file (int argc, AVal* argv, int i, const char* f,
    FILE** fp)
{
    AVal v = arg(argc, argv, i);
    if (!a_nat_is(v, FILE_KIND)) {
        return a_error("invalid argument to '%s' : expected file : got %s",
            f, a_typename(v));
    }
    *fp = a_nat_ptr(v);
    if (*fp == NULL) {
        return a_error("invalid argument to '%s' : closed file", f);
    }
    return A_OK;
}

// Pushes `nil, msg`, with the message of the last system error.
static AStatus fail (const char* what) {
    char msg[512];
    snprintf(msg, sizeof(msg), "%s : %s", what, strerror(errno));
    a_push(a_nil());
    a_push(a_strz(msg));
    return A_OK;
}

// io.open(path, mode): a file, or `nil, msg`. The mode is as in C,
// and defaults to "r".
static AStatus io_open (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AStr* path;
    AStr* mode = NULL;
    if (arg_str(argc, argv, 0, "open", &path) != A_OK) {
        return A_ERROR;
    }
    if (arg(argc,argv,1).type!=A_NIL &&
        arg_str(argc,argv,1,"open",&mode)!=A_OK)
    {
        return A_ERROR;
    }
    FILE* fp = fopen(path->s, (mode != NULL) ? mode->s : "r");
    if (fp == NULL) {
        return fail(path->s);
    }
    a_push(a_nat(fp, file_fin, FILE_KIND));
    return A_OK;
}

// io.close(f): closes the file now.
static AStatus io_close (AFunc* self, int argc, AVal* argv) {
    (void) self;
    FILE* fp;
    if (arg_file(argc, argv, 0, "close", &fp) != A_OK) {
        return A_ERROR;
    }
    a_nat_close(argv[0]);
    return A_OK;
}

// io.write(...) / io.write(f, ...): writes the values, as `tostring`
// shows them, to the standard output or to file `f`.
static AStatus io_write (AFunc* self, int argc, AVal* argv) {
    (void) self;
    FILE* fp = stdout;
    int i = 0;
    if (a_nat_is(arg(argc,argv,0), FILE_KIND)) {
        if (arg_file(argc, argv, 0, "write", &fp) != A_OK) {
            return A_ERROR;
        }
        i = 1;
    }
    for (; i<argc; i++) {
        AVal s = a_tostring(argv[i]);
        fwrite(A_STR_OF(s)->s, 1, A_STR_OF(s)->len, fp);
        a_dec(s);
    }
    return A_OK;
}

// Pushes the next line of the file, without the line break, or nil
// at the end.
static void read_line (FILE* fp) {
    Buf b = { NULL, 0, 0 };
    int any = 0;
    int c;
    while ((c = fgetc(fp)) != EOF) {
        any = 1;
        if (c == '\n') {
            break;
        }
        char ch = (char) c;
        buf_add(&b, &ch, 1);
    }
    if (any) {
        a_push(buf_val(&b));
    } else {
        a_push(a_nil());
    }
}

// io.read() / io.read(f): the next line of the standard input or of
// file `f`, or nil at the end.
// io.read(:all) / io.read(f, :all): all that remains, as one string.
static AStatus io_read (AFunc* self, int argc, AVal* argv) {
    (void) self;
    FILE* fp = stdin;
    int i = 0;
    if (a_nat_is(arg(argc,argv,0), FILE_KIND)) {
        if (arg_file(argc, argv, 0, "read", &fp) != A_OK) {
            return A_ERROR;
        }
        i = 1;
    }
    AVal mode = arg(argc, argv, i);
    if (mode.type == A_NIL) {
        read_line(fp);
        return A_OK;
    }
    if (mode.type!=A_STR || strcmp(A_STR_OF(mode)->s,"all")!=0) {
        return a_error("invalid argument to 'read' : expected file or :all");
    }
    Buf b = { NULL, 0, 0 };
    char chunk[4096];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
        buf_add(&b, chunk, n);
    }
    a_push(buf_val(&b));
    return A_OK;
}

// Iterator of `io.lines`: the file is its capture.
static AStatus lines_next (AFunc* self, int argc, AVal* argv) {
    (void) argc;
    (void) argv;
    FILE* fp = a_nat_ptr(self->caps[0]);
    if (fp == NULL) {
        a_push(a_nil());
    } else {
        read_line(fp);
    }
    return A_OK;
}

// io.lines(f): an iterator over the lines of a file, as in
// `loop l in io.lines(f) {...}`. Given a path instead, it opens the
// file, which closes itself when the iterator is released.
static AStatus io_lines (AFunc* self, int argc, AVal* argv) {
    (void) self;
    AVal f = arg(argc, argv, 0);
    if (f.type == A_STR) {
        FILE* fp = fopen(A_STR_OF(f)->s, "r");
        if (fp == NULL) {
            return a_error("invalid argument to 'lines' : %s : %s",
                A_STR_OF(f)->s, strerror(errno));
        }
        f = a_nat(fp, file_fin, FILE_KIND);
    } else {
        FILE* fp;
        if (arg_file(argc, argv, 0, "lines", &fp) != A_OK) {
            return A_ERROR;
        }
        a_inc(f);
    }
    AVal it = a_func(lines_next, 1);
    A_FUNC_OF(it)->caps[0] = f;
    a_func_scope(it);
    a_push(it);
    return A_OK;
}

void atm_lib_io_init (void) {
    atm_g_io = a_tab();
    field(atm_g_io, "open",  io_open);
    field(atm_g_io, "close", io_close);
    field(atm_g_io, "write", io_write);
    field(atm_g_io, "read",  io_read);
    field(atm_g_io, "lines", io_lines);
}

void atm_lib_io_quit (void) {
    a_clr(&atm_g_io);
}
