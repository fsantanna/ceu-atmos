# Dropped cases

Cases of the reference suite that this compiler does not
reproduce, with the reason found by `tst/extract.py`.

| Case | Origin | Reason |
|---|---|---|
| `0004-multiline-string` | exec.lua:37 | output differs |
| `0006-string-quote-via-multi-line` | exec.lua:51 | output differs |
| `0008-print-2-3` | exec.lua:63 | output differs |
| `0009-print-9-1-2` | exec.lua:68 | output differs |
| `0010-print-2-3-2` | exec.lua:73 | output differs |
| `0011-native-1` | exec.lua:87 | output differs |
| `0012-native-2` | exec.lua:95 | output differs |
| `0013-native-3` | exec.lua:105 | output differs |
| `0014-print-10` | exec.lua:110 | output differs |
| `0015-print-10-10` | exec.lua:115 | output differs |
| `0016-native-3` | exec.lua:123 | output differs |
| `0018-block-4` | exec.lua:164 | no expected output |
| `0019-block-5` | exec.lua:174 | output differs |
| `0021-do-2` | exec.lua:199 | output differs |
| `0030-test-2` | exec.lua:313 | output differs |
| `0034-defer-4` | exec.lua:369 | output differs |
| `0036-defer-6` | exec.lua:395 | no expected output |
| `0039-var-3` | exec.lua:430 | output differs |
| `0043-set-2-multi` | exec.lua:468 | output differs |
| `0046-table-1` | exec.lua:497 | output differs |
| `0047-print-1-1` | exec.lua:502 | output differs |
| `0049-print-1-1-1-1` | exec.lua:525 | output differs |
| `0050-x-print-1-1-1` | exec.lua:530 | output differs |
| `0055-vector-1` | exec.lua:564 | output differs |
| `0056-vector-2` | exec.lua:576 | output differs |
| `0057-vector-3` | exec.lua:589 | output differs |
| `0058-ppp-1` | exec.lua:605 | output differs |
| `0060-ppp-3` | exec.lua:634 | output differs |
| `0061-ppp-4` | exec.lua:649 | output differs |
| `0063-ppp-6-error-push` | exec.lua:678 | output differs |
| `0064-ppp-5` | exec.lua:702 | output differs |
| `0070-func-3` | exec.lua:792 | output differs |
| `0079-return` | exec.lua:921 | no expected output |
| `0081-func-9-err-val-func` | exec.lua:940 | output differs |
| `0101-match-6` | exec.lua:1115 | output differs |
| `0105-loop-2` | exec.lua:1173 | output differs |
| `0106-loop-num-1` | exec.lua:1183 | output differs |
| `0112-loop-5` | exec.lua:1243 | output differs |
| `0113-loop-6` | exec.lua:1252 | output differs |
| `0121-loop-12-break-crosses-func` | exec.lua:1362 | output differs |
| `0130-catch-8-err-goto` | exec.lua:1487 | output differs |
| `0131-catch-8b-err-escape` | exec.lua:1502 | output differs |
| `0133-catch-10` | exec.lua:1529 | output differs |
| `0135-catch-xx` | exec.lua:1554 | output differs |
| `0136-catch-12` | exec.lua:1572 | output differs |
| `0137-catch-13` | exec.lua:1591 | output differs |
| `0140-coro-1` | exec.lua:1651 | output differs |
| `0141-emit-1-err` | exec.lua:1658 | output differs |
| `0142-task-1` | exec.lua:1677 | output differs |
| `0144-coro-2-multi` | exec.lua:1704 | output differs |
| `0145-coro-3-multi` | exec.lua:1727 | output differs |
| `0146-coro-4` | exec.lua:1741 | output differs |
| `0147-coro-5` | exec.lua:1755 | output differs |
| `0148-emit-1` | exec.lua:1768 | no expected output |
| `0151-task-throw-catch-1` | exec.lua:1807 | known failure in the reference |
| `0160-task-throw` | exec.lua:1949 | output differs |
| `0164-tasks-2` | exec.lua:2001 | output differs |
| `0171-tasks-9-max` | exec.lua:2102 | output differs |
| `0172-tasks-10-max` | exec.lua:2115 | output differs |
| `0181-await-task-4-throw-dbg-loc` | exec.lua:2268 | output differs |
| `0187-await-1` | exec.lua:2392 | output differs |
| `0192-toggle-2` | exec.lua:2464 | output differs |
| `0193-toggle-3` | exec.lua:2493 | output differs |
| `0202-is-1` | exec.lua:2655 | output differs |
| `0203-is-1` | exec.lua:2660 | output differs |
| `0216-func-4` | exec.lua:2799 | output differs |
| `0217-error-hello` | exec.lua:2809 | output differs |
| `0218-error-1` | exec.lua:2823 | output differs |
| `0219-error-2` | exec.lua:2835 | no expected output |
| `0220-require-1` | exec.lua:2848 | output differs |
| `0221-require-2-error` | exec.lua:2858 | output differs |
| `0223-print-0xa-10` | x.lua:8 | output differs |
| `0224-print-1-2-3-1-2-3` | x.lua:13 | output differs |
| `0225-print` | x.lua:18 | output differs |
| `0226-print-x-1-x-1` | x.lua:23 | output differs |
| `0227-expr` | x.lua:39 | output differs |
| `0228-expr-table` | x.lua:49 | output differs |
| `0229-expr-vector` | x.lua:59 | output differs |
| `0230-expr-vector` | x.lua:69 | output differs |
| `0231-gte-1` | x.lua:89 | output differs |
| `0232-lte-1` | x.lua:102 | output differs |
| `0235-cat-3-vector` | x.lua:132 | output differs |
| `0239-is-3` | x.lua:175 | output differs |
| `0240-is-3b-xtask-non-proto-fails` | x.lua:183 | output differs |
| `0241-is-4` | x.lua:197 | output differs |
| `0242-in-1` | x.lua:208 | output differs |
| `0244-case` | x.lua:227 | output differs |
| `0247-task-2-proto-equal` | tasks.lua:119 | no expected output |
| `0248-task-3-coro-proto-xtask` | tasks.lua:130 | output differs |
| `0251-task-5-bare-task-me` | tasks.lua:164 | output differs |
| `0255-task-8b-me-as-emit-payload` | tasks.lua:234 | output differs |
| `0257-yield-1-no-enclosing-exec` | tasks.lua:252 | no expected output |
| `0258-spawn-1` | tasks.lua:265 | no expected output |
| `0259-spawn-2` | tasks.lua:274 | known failure in the reference |
| `0262-spawn-neg-direct-call-proto-fails` | tasks.lua:302 | output differs |
| `0264-yield-2-error-yield-inside-task` | tasks.lua:330 | output differs |
| `0265-resume-1-error-resume-task` | tasks.lua:351 | known failure in the reference |
| `0268-spawn-3-error` | tasks.lua:377 | output differs |
| `0277-spawn-5` | tasks.lua:519 | output differs |
| `0281-spawn-5b-4` | tasks.lua:564 | output differs |
| `0284-spawn-7` | tasks.lua:612 | output differs |
| `0285-spawn-7` | tasks.lua:623 | output differs |
| `0287-emit-2-task-term` | tasks.lua:662 | no expected output |
| `0290-emit-5` | tasks.lua:712 | no expected output |
| `0294-emit-9` | tasks.lua:793 | output differs |
| `0295-emit-10` | tasks.lua:814 | output differs |
| `0297-emit-12` | tasks.lua:844 | output differs |
| `0298-emit-13` | tasks.lua:864 | output differs |
| `0299-emit-scope-1` | tasks.lua:882 | output differs |
| `0300-emit-scope-2` | tasks.lua:896 | output differs |
| `0301-emit-scope-3` | tasks.lua:910 | output differs |
| `0303-emit-scope-5` | tasks.lua:941 | output differs |
| `0304-emit-scope-6` | tasks.lua:968 | output differs |
| `0305-emit-scope-7` | tasks.lua:996 | output differs |
| `0306-emit-scope-8` | tasks.lua:1010 | output differs |
| `0307-emit-scope-9` | tasks.lua:1027 | output differs |
| `0308-emit-scope-10` | tasks.lua:1051 | output differs |
| `0309-emit-scope-11` | tasks.lua:1073 | output differs |
| `0310-emit-scope-12` | tasks.lua:1095 | output differs |
| `0311-emit-scope-13` | tasks.lua:1123 | output differs |
| `0313-alien-1` | tasks.lua:1154 | output differs |
| `0314-alien-2` | tasks.lua:1170 | output differs |
| `0315-alien-3` | tasks.lua:1186 | output differs |
| `0316-alien-4` | tasks.lua:1204 | output differs |
| `0317-alien-5` | tasks.lua:1220 | output differs |
| `0318-alien-6` | tasks.lua:1235 | output differs |
| `0319-alien-7` | tasks.lua:1251 | output differs |
| `0321-payload-2` | tasks.lua:1294 | output differs |
| `0323-payload-4` | tasks.lua:1332 | output differs |
| `0327-payload-8-multi-emit-await-args` | tasks.lua:1403 | output differs |
| `0328-payload-9` | tasks.lua:1417 | output differs |
| `0332-order-3` | tasks.lua:1492 | no expected output |
| `0338-pin-1` | tasks.lua:1583 | output differs |
| `0347-task-term-3` | tasks.lua:1711 | no expected output |
| `0348-task-term-4` | tasks.lua:1731 | output differs |
| `0356-pub-7` | tasks.lua:1840 | output differs |
| `0359-nested-3` | tasks.lua:1887 | output differs |
| `0365-abort-4-explicit-self-abort` | tasks.lua:1994 | output differs |
| `0392-abort-25` | tasks.lua:2605 | known failure in the reference |
| `0401-abort-34` | tasks.lua:2826 | output differs |
| `0404-abort-37` | tasks.lua:2899 | output differs |
| `0415-return-1` | tasks.lua:3143 | output differs |
| `0416-return-2` | tasks.lua:3160 | output differs |
| `0425-tasks-8` | tasks.lua:3273 | output differs |
| `0438-tasks-21-pool-limit-fail-then-pass` | tasks.lua:3539 | output differs |
| `0441-tasks-24` | tasks.lua:3591 | output differs |
| `0444-every-2` | tasks.lua:3638 | output differs |
| `0445-every-where` | tasks.lua:3654 | output differs |
| `0475-op-mixed-err-1` | await.lua:117 | output differs |
| `0479-task-promote-nil-arg-1` | await.lua:183 | output differs |
| `0489-timers-3-periodic-catch-up` | timers.lua:47 | output differs |
| `0495-timers-9-next` | timers.lua:139 | output differs |
| `0504-beh-1` | streams.lua:50 | output differs |
| `0505-beh-2` | streams.lua:68 | output differs |
| `0506-beh-3` | streams.lua:91 | output differs |
| `0507-func-1` | streams.lua:108 | output differs |
| `0508-func-2` | streams.lua:121 | output differs |
| `0509-func-3` | streams.lua:139 | output differs |
| `0510-func-4` | streams.lua:158 | output differs |
| `0511-func-5` | streams.lua:169 | output differs |
| `0512-func-6` | streams.lua:183 | output differs |
| `0513-func-7` | streams.lua:193 | output differs |
| `0514-func-8` | streams.lua:206 | output differs |
| `0515-func-9` | streams.lua:221 | output differs |
| `0516-thread-exec-00` | thread.lua:63 | output differs |
| `0517-thread-exec-01` | thread.lua:79 | output differs |
| `0518-thread-exec-02` | thread.lua:100 | output differs |
| `0519-thread-exec-03` | thread.lua:143 | output differs |
