# Adapted cases

Cases of the reference suite that were adapted to this
language, according to `tst/adapt.py`.

- Rewritten: the source changed, and the output is the one
  the reference expects.
- Accepted: the output differs from the reference by design.

| Case | Rewritten | Accepted |
|---|---|---|
| `0008-print-2-3` | operators that became functions |  |
| `0009-print-9-1-2` | operators that became functions |  |
| `0010-print-2-3-2` | rewritten |  |
| `0021-do-2` |  | an expression may be a statement |
| `0039-var-3` |  | the error has another message |
| `0043-set-2-multi` |  | val is immutable, also in a multiple declaration |
| `0046-table-1` |  | indexes start at 0 |
| `0047-print-1-1` |  | the error has another message |
| `0049-print-1-1-1-1` | rewritten |  |
| `0050-x-print-1-1-1` | rewritten |  |
| `0055-vector-1` | rewritten |  |
| `0056-vector-2` | rewritten |  |
| `0057-vector-3` | rewritten |  |
| `0058-ppp-1` | rewritten |  |
| `0060-ppp-3` | rewritten |  |
| `0061-ppp-4` | rewritten |  |
| `0063-ppp-6-error-push` | rewritten | indexes start at 0 |
| `0077-func-7-nested` | rewritten |  |
| `0078-func-8-nested` | rewritten |  |
| `0105-loop-2` |  | indexes start at 0 |
| `0106-loop-num-1` |  | indexes start at 0 |
| `0112-loop-5` |  | indexes start at 0 |
| `0113-loop-6` |  | indexes start at 0 |
| `0121-loop-12-break-crosses-func` |  | break is lexical |
| `0131-catch-8b-err-escape` |  | the error has another message |
| `0164-tasks-2` |  | tasks and pools have their own types |
| `0171-tasks-9-max` | rewritten |  |
| `0172-tasks-10-max` | rewritten |  |
| `0178-await-task-1` | a spawn is not the last statement of its block |  |
| `0180-await-task-3` | a spawn is not the last statement of its block |  |
| `0202-is-1` |  | the error has another message |
| `0203-is-1` |  | the error has another message |
| `0216-func-4` |  | the error has another message |
| `0223-print-0xa-10` | operators that became functions |  |
| `0224-print-1-2-3-1-2-3` | operators that became functions |  |
| `0225-print` | operators that became functions |  |
| `0226-print-x-1-x-1` | operators that became functions |  |
| `0228-expr-table` | operators that became functions |  |
| `0229-expr-vector` | operators that became functions |  |
| `0230-expr-vector` | operators that became functions |  |
| `0231-gte-1` | operators that became functions |  |
| `0232-lte-1` | operators that became functions |  |
| `0235-cat-3-vector` | rewritten |  |
| `0241-is-4` | operators that became functions | ?? compares tables field by field |
| `0242-in-1` | operators that became functions |  |
| `0244-case` | rewritten |  |
| `0253-task-7-ancestor-alias-in-nested-task` | a spawn is not the last statement of its block |  |
| `0254-task-8-non-ancestor-alias-rejected` | a spawn is not the last statement of its block |  |
| `0255-task-8b-me-as-emit-payload` | indexes start at 0, a spawn is not the last statement of its block |  |
| `0262-spawn-neg-direct-call-proto-fails` |  | the error has another message |
| `0263-spawn-neg-await-plain-func-fails` | a spawn is not the last statement of its block |  |
| `0277-spawn-5` | rewritten | a terminated task needs no owner |
| `0281-spawn-5b-4` |  | a terminated task needs no owner |
| `0282-spawn-5c` | a spawn is not the last statement of its block |  |
| `0294-emit-9` | indexes start at 0 |  |
| `0295-emit-10` | indexes start at 0 |  |
| `0297-emit-12` | indexes start at 0 |  |
| `0299-emit-scope-1` | indexes start at 0 |  |
| `0300-emit-scope-2` | indexes start at 0 |  |
| `0301-emit-scope-3` | indexes start at 0 |  |
| `0303-emit-scope-5` | indexes start at 0 |  |
| `0304-emit-scope-6` | rewritten | catch does not stop a return |
| `0305-emit-scope-7` | indexes start at 0 |  |
| `0306-emit-scope-8` | indexes start at 0 |  |
| `0307-emit-scope-9` | indexes start at 0 |  |
| `0308-emit-scope-10` | indexes start at 0 |  |
| `0309-emit-scope-11` | indexes start at 0 |  |
| `0310-emit-scope-12` | indexes start at 0 |  |
| `0313-alien-1` | indexes start at 0 |  |
| `0314-alien-2` | indexes start at 0 |  |
| `0315-alien-3` | indexes start at 0 |  |
| `0316-alien-4` | indexes start at 0 |  |
| `0317-alien-5` | indexes start at 0 |  |
| `0318-alien-6` | indexes start at 0 |  |
| `0319-alien-7` | indexes start at 0 |  |
| `0321-payload-2` | rewritten |  |
| `0323-payload-4` | indexes start at 0 |  |
| `0327-payload-8-multi-emit-await-args` | indexes start at 0 |  |
| `0328-payload-9` | indexes start at 0 |  |
| `0335-emit-in-2` | a spawn is not the last statement of its block |  |
| `0336-emit-in-3` | a spawn is not the last statement of its block |  |
| `0338-pin-1` |  | a terminated task needs no owner |
| `0348-task-term-4` | rewritten |  |
| `0356-pub-7` | indexes start at 0 |  |
| `0357-nested-1` | a spawn is not the last statement of its block |  |
| `0368-abort-2` | a spawn is not the last statement of its block |  |
| `0379-abort-12` | a spawn is not the last statement of its block |  |
| `0423-tasks-7-error-bare-pool-needs-any-all` | a spawn is not the last statement of its block |  |
| `0425-tasks-8` |  | tasks and pools have their own types |
| `0438-tasks-21-pool-limit-fail-then-pass` | rewritten | a terminated task leaves its pool at once |
| `0444-every-2` | indexes start at 0 |  |
| `0458-par-any-3-return` | a spawn is not the last statement of its block |  |
| `0459-par-all-3-return` | a spawn is not the last statement of its block |  |
| `0460-watching-2` | a spawn is not the last statement of its block |  |
| `0462-watching-f-error-unexpected-function` |  | the error points to the expression |
| `0475-op-mixed-err-1` | a spawn is not the last statement of its block | await combinators follow the precedence of C |
| `0478-task-promote-solo-1` | a spawn is not the last statement of its block |  |
| `0479-task-promote-nil-arg-1` | a spawn is not the last statement of its block |  |
| `0484-task-promote-nontask-err-1` | a spawn is not the last statement of its block |  |
| `0489-timers-3-periodic-catch-up` |  | indexes start at 0 |
