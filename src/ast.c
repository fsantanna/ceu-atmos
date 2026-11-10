// Atmos compiler: AST dump as S-expressions (for parser tests).

#include <stdio.h>

#include "atmc.h"

static void dump_patt (Patt* p) {
    switch (p->kind) {
        case PT_VAL:
            ast_dump(p->e);
            break;
        case PT_SPAWN:
            printf("(spawn ");
            ast_dump(p->e);
            for (int i=0; i<p->args.n; i++) {
                printf(" ");
                ast_dump(p->args.v[i]);
            }
            printf(")");
            break;
        case PT_ANY:
        case PT_ALL:
            printf("(%s ", p->tk.str);
            ast_dump(p->e);
            printf(")");
            break;
        case PT_UNTIL:
        case PT_WHILE:
            printf("(%s ", p->tk.str);
            if (p->a != NULL) {
                dump_patt(p->a);
                printf(" ");
            }
            ast_dump(p->e);
            printf(")");
            break;
        case PT_NOT:
            printf("(! ");
            dump_patt(p->a);
            printf(")");
            break;
        case PT_AND:
        case PT_OR:
            printf("(%s ", p->tk.str);
            dump_patt(p->a);
            printf(" ");
            dump_patt(p->b);
            printf(")");
            break;
    }
}

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
            if (e->u.loop.on != NULL) {
                printf(" on ");
                dump_patt(e->u.loop.on);
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
        case E_ME:
            printf("(me)");
            break;
        case E_PUB:
            printf("pub");
            break;
        case E_SPAWN:
            printf("(spawn ");
            if (e->u.spawn.pool != NULL) {
                printf("@");
                ast_dump(e->u.spawn.pool);
                printf(" ");
            }
            ast_dump(e->u.spawn.f);
            dump_list(&e->u.spawn.args);
            printf(")");
            break;
        case E_SPAWN_BLK:
            printf("(spawn ");
            ast_dump(e->u.sblk.blk);
            printf(")");
            break;
        case E_TASKS:
            printf("(tasks");
            if (e->u.tasks.n != NULL) {
                printf(" ");
                ast_dump(e->u.tasks.n);
            }
            printf(")");
            break;
        case E_EMIT:
            printf("(emit ");
            if (e->u.emit.to != NULL) {
                printf("@");
                ast_dump(e->u.emit.to);
                printf(" ");
            }
            ast_dump(e->u.emit.e);
            printf(")");
            break;
        case E_AWAIT:
            printf("(await ");
            dump_patt(e->u.await.p);
            printf(")");
            break;
        case E_TOGGLE:
            printf("(toggle ");
            ast_dump(e->u.toggle.t);
            dump_list(&e->u.toggle.args);
            for (int i=0; i<e->u.toggle.nf; i++) {
                printf(" with ");
                dump_patt(e->u.toggle.fs[i]);
            }
            printf(")");
            break;
        case E_TOGGLE_ON:
            printf("(toggle on %s", e->tk.str);
            for (int i=0; i<e->u.toggle_on.nf; i++) {
                printf(" with ");
                dump_patt(e->u.toggle_on.fs[i]);
            }
            printf(" ");
            ast_dump(e->u.toggle_on.blk);
            printf(")");
            break;
        case E_PAR:
            printf("(par%s", (e->u.par.mode == PAR_ANY) ? " :any" :
                             (e->u.par.mode == PAR_ALL) ? " :all" : "");
            dump_list(&e->u.par.blks);
            printf(")");
            break;
        case E_WATCHING:
            printf("(watching ");
            dump_patt(e->u.watching.p);
            printf(" ");
            ast_dump(e->u.watching.blk);
            printf(")");
            break;
    }
}
