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
	# The status is checked, and it used to be thrown away deliberately:
	# `if "$@" | tee -a "$OUT"; then :; else :; fi`. A pipeline's status is
	# the last command's, so that was tee's -- but even capturing it would
	# not have been enough, because the score is counted by grepping for
	# "ok - " lines. A test binary that dies partway through leaves its
	# earlier ok lines in the file, emits no "not ok", and the suite prints
	# "N passed, 0 failed" with a smaller N that nothing asserts. The
	# 3584/3584 table check and the 52 corpus cases can both silently
	# become a handful.
	#
	# Under test-asan this is the difference between a sanitizer report and
	# a green run: ASan exits 1, which went straight into the bit bucket.
	# tzf/t/run.sh in the same directory already does it this way.
	out=$("$@" 2>&1)
	rc=$?
	printf '%s\n' "$out" | tee -a "$OUT"
	if [ "$rc" -ne 0 ]; then
		printf 'not ok - %s exited %s (crash, abort or sanitizer)\n' \
			"$label" "$rc" | tee -a "$OUT"
	fi
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
build ./t/grapheme_test t/grapheme_test.c vtparse.c
build ./t/utf8_dump t/utf8_dump.c vtparse.c

run "oracle 1: published state table (14 states x 256 bytes)" ./t/table_test
run "oracle 4: real escape sequence corpus" ./t/corpus_test
run "oracle 2: width vs libc wcwidth(3) over the BMP" ./t/width_diff
run "oracle 3: UTF-8 decode vs CPython" python3 t/utf8_diff.py ./t/utf8_dump

# The UCD is not in this repository; on this machine it is a flatpak host
# mount.  Say so before the test runs, so that a run without it does not look
# like a run with it: the eleven per-rule assertions and the 853-case score
# simply are not in the total, and a shrinking denominator that nothing
# announces is how a conformance number stops meaning anything.
GBT=${VTPARSE_UCD:-/run/host/usr/share/unicode/ucd}/auxiliary/GraphemeBreakTest.txt
if [ ! -f "$GBT" ]; then
	printf '\n!!! SKIP: %s is missing.\n' "$GBT"
	printf '!!! UAX #29 conformance is NOT CHECKED on this run; 15 of the\n'
	printf '!!! suite'"'"'s assertions are absent, not passing.\n'
fi
run "oracle 5: UAX #29 vs GraphemeBreakTest.txt" ./t/grapheme_test

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
