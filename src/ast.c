// Atmos compiler: AST dump as S-expressions (for parser tests).

#include <stdio.h>

#include "atmc.h"

static void dump_list (List* l) {
    for (int i=0; i<l->n; i++) {
        printf(" ");
        ast_dump(l->v[i]);
    }
}

void ast_dump (Expr* e) {
    if (e == NULL) {
        printf("_");
        return;
    }
    switch (e->tag) {
        case E_NIL:  printf("nil");                     break;
        case E_BOOL: printf("%s", e->tk.str);           break;
        case E_NUM:  printf("%s", e->tk.str);           break;
        case E_STR:  printf("\"%s\"", e->tk.str);       break;
        case E_TAG:  printf("%s", e->tk.str);           break;
        case E_NAT:  printf("`%s`", e->tk.str);         break;
        case E_CLK:  printf("%s", e->tk.str);           break;
        case E_DOTS: printf("...");                     break;
        case E_ACC:  printf("%s", e->tk.str);           break;
        case E_INDEX:
            printf("(@ ");
            ast_dump(e->u.index.t);
            printf(" ");
            switch (e->u.index.kind) {
                case IDX_EXPR: ast_dump(e->u.index.idx); break;
                case IDX_LAST: printf("#");              break;
                case IDX_NEXT: printf("+");              break;
            }
            printf(")");
            break;
        case E_TABLE:
            printf("[");
            for (int i=0; i<e->u.table.vs.n; i++) {
                if (i > 0) {
                    printf(" ");
                }
                if (e->u.table.ks.v[i] != NULL) {
                    ast_dump(e->u.table.ks.v[i]);
                    printf("=");
                }
                ast_dump(e->u.table.vs.v[i]);
            }
            printf("]");
            break;
        case E_TAGGED:
            printf("(tag %s ", e->tk.str);
            ast_dump(e->u.tagged.e);
            printf(")");
            break;
        case E_UNO:
            printf("(%s ", e->tk.str);
            ast_dump(e->u.uno.e);
            printf(")");
            break;
        case E_BIN:
            printf("(%s ", e->tk.str);
            ast_dump(e->u.bin.e1);
            printf(" ");
            ast_dump(e->u.bin.e2);
            printf(")");
            break;
        case E_CALL:
            printf("(call ");
            ast_dump(e->u.call.f);
            dump_list(&e->u.call.es);
            printf(")");
            break;
        case E_MET:
            printf("(:: ");
            ast_dump(e->u.met.o);
            printf(" %s)", e->u.met.met.str);
            break;
        case E_PARENS:
            printf("(parens ");
            ast_dump(e->u.parens.e);
            printf(")");
            break;
        case E_LIST:
            printf("(list");
            dump_list(&e->u.list.es);
            printf(")");
            break;
        case E_WHERE:
            printf("(where ");
            ast_dump(e->u.where.e);
            dump_list(&e->u.where.dcls);
            printf(")");
            break;
        case E_DCL:
            printf("(%s", e->tk.str);
            for (int i=0; i<e->u.dcl.n; i++) {
                printf(" %s", e->u.dcl.ids[i].str);
            }
            if (e->u.dcl.set != NULL) {
                printf(" = ");
                ast_dump(e->u.dcl.set);
            }
            printf(")");
            break;
        case E_SET:
            printf("(set");
            dump_list(&e->u.set.dsts);
            printf(" = ");
            ast_dump(e->u.set.src);
            printf(")");
            break;
        case E_BLOCK:
            printf("{");
            dump_list(&e->u.block.es);
            printf(" }");
            break;
        case E_DO:
            printf("(do ");
            if (e->u.xdo.esc) {
                printf("%s ", e->tk.str);
            }
            ast_dump(e->u.xdo.blk);
            printf(")");
            break;
        case E_DEFER:
            printf("(defer ");
            ast_dump(e->u.defer.blk);
            printf(")");
            break;
        case E_IFS:
            printf("(%s", (e->u.ifs.head != NULL) ? "match " : "ifs");
            if (e->u.ifs.head != NULL) {
                ast_dump(e->u.ifs.head);
            }
            for (int i=0; i<e->u.ifs.n; i++) {
                Case* c = &e->u.ifs.cases[i];
                printf(" (");
                if (c->cnd == NULL) {
                    printf("else");
                } else {
                    if (c->test) {
                        printf("test ");
                    }
                    ast_dump(c->cnd);
                }
                printf(" => ");
                ast_dump(c->body);
                printf(")");
            }
            printf(")");
            break;
        case E_LOOP:
            printf("(loop");
            for (int i=0; i<e->u.loop.n; i++) {
                printf(" %s", e->u.loop.ids[i].str);
            }
            if (e->u.loop.itr != NULL) {
                printf(" in ");
                ast_dump(e->u.loop.itr);
            }
            printf(" ");
            ast_dump(e->u.loop.blk);
            printf(")");
            break;
        case E_CATCH:
            printf("(catch ");
            ast_dump(e->u.xcatch.cnd);
            printf(" ");
            ast_dump(e->u.xcatch.blk);
            printf(")");
            break;
        case E_PROTO:
            printf("(%s (", e->u.proto.task ? "task" : "func");
            for (int i=0; i<e->u.proto.n; i++) {
                printf("%s%s", (i > 0) ? " " : "", e->u.proto.pars[i].str);
            }
            if (e->u.proto.dots) {
                printf("%s...", (e->u.proto.n > 0) ? " " : "");
            }
            printf(") ");
            ast_dump(e->u.proto.blk);
            printf(")");
            break;
        case E_RETURN:
            printf("(return");
            dump_list(&e->u.ret.es);
            printf(")");
            break;
        case E_ME:
            printf("(me)");
            break;
    }
}
