#!/bin/sh
# usage: run_cli_tests.sh <ikcas binary>
#
# Every tests/cli/NAME.in is fed to the binary on stdin and its stdout and
# stderr are compared with NAME.out. An optional NAME.args holds extra
# command-line arguments. Run from the project root.
bin=$1
dir=$(dirname "$0")/cli
fail=0

for in in "$dir"/*.in; do
    base=${in%.in}
    args=
    [ -f "$base.args" ] && args=$(cat "$base.args")
    actual=$($bin $args < "$in" 2>&1)
    expected=$(cat "$base.out")
    if [ "$actual" = "$expected" ]; then
        echo "PASS cli/$(basename "$base")"
    else
        echo "FAIL cli/$(basename "$base")"
        printf '%s\n' "$actual" | diff - "$base.out"
        fail=1
    fi
done
exit $fail
