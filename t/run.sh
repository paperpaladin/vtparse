#!/bin/sh
# t/run.sh -- build and run every oracle, print "N passed, M failed".
# SPDX-License-Identifier: GPL-3.0-or-later
#
# This script, not the Makefile, compiles the test programs: `make test-asan`
# just re-invokes it with a different CFLAGS, and there is nothing else to keep
# in sync.  POSIX sh, because the only thing bash would add here is bashisms.
set -u

cd "$(dirname "$0")/.." || exit 1

CC=${CC:-gcc}
CFLAGS=${CFLAGS:--std=c11 -Wall -Wextra -Wpedantic -O2 -g}
OUT=$(mktemp)
trap 'rm -f "$OUT"' EXIT

say() { printf '%s\n' "$*" | tee -a "$OUT"; }
run() { # run <label> <command...>
	label=$1
	shift
	printf '\n--- %s\n' "$label"
	if "$@" 2>&1 | tee -a "$OUT"; then :; else :; fi
}

build() { # build <output> <sources...>
	out=$1
	shift
	# shellcheck disable=SC2086
	if ! $CC $CFLAGS -o "$out" "$@"; then
		say "not ok - compile $out"
		return 1
	fi
	return 0
}

printf '=== building (CC=%s CFLAGS=%s)\n' "$CC" "$CFLAGS"
build ./vtparse main.c vtparse.c
build ./t/table_test t/table_test.c vtparse.c
build ./t/corpus_test t/corpus_test.c vtparse.c
build ./t/width_diff t/width_diff.c vtparse.c
build ./t/utf8_dump t/utf8_dump.c vtparse.c

run "oracle 1: published state table (14 states x 256 bytes)" ./t/table_test
run "oracle 4: real escape sequence corpus" ./t/corpus_test
run "oracle 2: width vs libc wcwidth(3) over the BMP" ./t/width_diff
run "oracle 3: UTF-8 decode vs CPython" python3 t/utf8_diff.py ./t/utf8_dump

# --- CLI golden output ------------------------------------------------------
printf '\n--- CLI event stream\n'
printf '\033[1;31mhi\033]0;win\033\\\343\201\202\033P+q544e\033\\\n' \
	| ./vtparse -u > ./t/cli.got 2>&1
if diff -u ./t/cli.expected ./t/cli.got; then
	say "ok - CLI event stream matches t/cli.expected"
else
	say "not ok - CLI event stream matches t/cli.expected"
fi

# --- it must survive hostile input -----------------------------------------
# The whole point of a non-allocating parser is that untrusted bytes cannot
# make it misbehave; under `make test-asan` this is where that gets proved.
printf '\n--- random input smoke test\n'
for mode in '' '-u'; do
	# shellcheck disable=SC2086
	if head -c 400000 /dev/urandom | ./vtparse $mode > /dev/null 2>&1; then
		say "ok - 400kB of /dev/urandom through vtparse ${mode:-(8-bit)}"
	else
		say "not ok - 400kB of /dev/urandom through vtparse ${mode:-(8-bit)}"
	fi
done

pass=$(grep -c '^ok - ' "$OUT")
fail=$(grep -c '^not ok - ' "$OUT")
printf '\n%s passed, %s failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
