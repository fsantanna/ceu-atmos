#!/bin/bash

# Runs the golden tests.
#
#   tst/run.sh            all suites
#   tst/run.sh lex        one suite (lex, ast)
#
# Each case is a `NAME.atm` with the expected output in `NAME.out`
# (stdout followed by stderr).

cd "$(dirname "$0")"

ATMOS=../../atmos

suites=${@:-lex ast}

pass=0
fail=0

for suite in $suites; do
    case $suite in
        lex) flags="--lex" ;;
        ast) flags="--ast" ;;
        *)   echo "unknown suite: $suite" ; exit 1 ;;
    esac
    cd $suite
    for atm in *.atm; do
        name=${atm%.atm}
        out=$($ATMOS $flags $atm 2>&1)
        if [ "$out" == "$(cat $name.out)" ]; then
            pass=$((pass+1))
        else
            fail=$((fail+1))
            echo "FAIL: $suite/$name"
            diff <(echo "$out") $name.out | head -10
        fi
    done
    cd ..
done

echo "pass=$pass fail=$fail"
[ $fail -eq 0 ]
