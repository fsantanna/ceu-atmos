# Dropped cases

Cases of the reference suite that this compiler does not
reproduce, with the reason found by `tst/extract.py`.

| Case | Origin | Reason |
|---|---|---|
| `0004-multiline-string` | exec.lua:37 | global of the test harness |
| `0006-string-quote-via-multi-line` | exec.lua:51 | global of the test harness |
| `0011-native-1` | exec.lua:87 | native with Lua code |
| `0012-native-2` | exec.lua:95 | native with Lua code |
| `0013-native-3` | exec.lua:105 | native with Lua code |
| `0014-print-10` | exec.lua:110 | native with Lua code |
| `0015-print-10-10` | exec.lua:115 | native with Lua code |
| `0016-native-3` | exec.lua:123 | native with Lua code |
| `0018-block-4` | exec.lua:164 | no expected output |
| `0019-block-5` | exec.lua:174 | globals are static |
| `0036-defer-6` | exec.lua:395 | no expected output |
| `0070-func-3` | exec.lua:792 | globals are static |
| `0079-return` | exec.lua:921 | no expected output |
| `0081-func-9-err-val-func` | exec.lua:940 | globals are static |
| `0133-catch-10` | exec.lua:1529 | globals are static |
| `0140-coro-1` | exec.lua:1651 | Lua library |
| `0141-emit-1-err` | exec.lua:1658 | globals are static |
| `0142-task-1` | exec.lua:1677 | xtask constructor |
| `0144-coro-2-multi` | exec.lua:1704 | Lua library |
| `0145-coro-3-multi` | exec.lua:1727 | Lua library |
| `0146-coro-4` | exec.lua:1741 | Lua library |
| `0147-coro-5` | exec.lua:1755 | Lua library |
| `0148-emit-1` | exec.lua:1768 | no expected output |
| `0151-task-throw-catch-1` | exec.lua:1807 | known failure in the reference |
| `0187-await-1` | exec.lua:2392 | globals are static |
| `0192-toggle-2` | exec.lua:2464 | xtask constructor |
| `0193-toggle-3` | exec.lua:2493 | xtask constructor |
| `0217-error-hello` | exec.lua:2809 | Lua library |
| `0218-error-1` | exec.lua:2823 | globals are static |
| `0219-error-2` | exec.lua:2835 | no expected output |
| `0220-require-1` | exec.lua:2848 | Lua library |
| `0221-require-2-error` | exec.lua:2858 | Lua library |
| `0227-expr` | x.lua:39 | operator that became a function |
| `0239-is-3` | x.lua:175 | xtask constructor |
| `0240-is-3b-xtask-non-proto-fails` | x.lua:183 | xtask constructor |
| `0247-task-2-proto-equal` | tasks.lua:119 | no expected output |
| `0248-task-3-coro-proto-xtask` | tasks.lua:130 | Lua library |
| `0251-task-5-bare-task-me` | tasks.lua:164 | xtask constructor |
| `0257-yield-1-no-enclosing-exec` | tasks.lua:252 | no expected output |
| `0258-spawn-1` | tasks.lua:265 | no expected output |
| `0259-spawn-2` | tasks.lua:274 | known failure in the reference |
| `0264-yield-2-error-yield-inside-task` | tasks.lua:330 | Lua library |
| `0265-resume-1-error-resume-task` | tasks.lua:351 | known failure in the reference |
| `0284-spawn-7` | tasks.lua:612 | xtask constructor |
| `0285-spawn-7` | tasks.lua:623 | xtask constructor |
| `0287-emit-2-task-term` | tasks.lua:662 | no expected output |
| `0290-emit-5` | tasks.lua:712 | no expected output |
| `0298-emit-13` | tasks.lua:864 | native with Lua code |
| `0311-emit-scope-13` | tasks.lua:1123 | native with Lua code |
| `0332-order-3` | tasks.lua:1492 | no expected output |
| `0347-task-term-3` | tasks.lua:1711 | no expected output |
| `0359-nested-3` | tasks.lua:1887 | Lua library |
| `0365-abort-4-explicit-self-abort` | tasks.lua:1994 | native with Lua code |
| `0392-abort-25` | tasks.lua:2605 | known failure in the reference |
| `0401-abort-34` | tasks.lua:2826 | Lua library |
| `0404-abort-37` | tasks.lua:2899 | xtask constructor |
| `0441-tasks-24` | tasks.lua:3591 | operator that became a function |
| `0445-every-where` | tasks.lua:3654 | globals are static |
| `0495-timers-9-next` | timers.lua:139 | scheduler API of lua-atmos |
| `0504-beh-1` | streams.lua:50 | Lua library |
| `0505-beh-2` | streams.lua:68 | Lua library |
| `0506-beh-3` | streams.lua:91 | Lua library |
| `0507-func-1` | streams.lua:108 | Lua library |
| `0508-func-2` | streams.lua:121 | Lua library |
| `0509-func-3` | streams.lua:139 | Lua library |
| `0510-func-4` | streams.lua:158 | Lua library |
| `0511-func-5` | streams.lua:169 | Lua library |
| `0512-func-6` | streams.lua:183 | Lua library |
| `0513-func-7` | streams.lua:193 | Lua library |
| `0514-func-8` | streams.lua:206 | Lua library |
| `0515-func-9` | streams.lua:221 | Lua library |
| `0516-thread-exec-00` | thread.lua:63 | native with Lua code |
| `0517-thread-exec-01` | thread.lua:79 | native with Lua code |
| `0518-thread-exec-02` | thread.lua:100 | native with Lua code |
| `0519-thread-exec-03` | thread.lua:143 | native with Lua code |
