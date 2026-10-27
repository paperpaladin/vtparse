# vtparse -- SPDX-License-Identifier: GPL-3.0-or-later
#
# t/run.sh does the test-program compiling, so the sanitizer target is a CFLAGS
# override and nothing more.

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g -ffile-prefix-map=$(CURDIR)=.
ASAN     = -std=c11 -Wall -Wextra -Wpedantic -O1 -g \
	   -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: all test test-asan test-valgrind gen-width gen-grapheme clean

all: vtparse

vtparse: main.c vtparse.c vtparse.h width_table.h grapheme_table.h
	$(CC) $(CFLAGS) -o $@ main.c vtparse.c

test: all
	CC='$(CC)' CFLAGS='$(CFLAGS)' ./t/run.sh

# We parse untrusted bytes, so the sanitizers are not optional diligence.
test-asan:
	CC='$(CC)' CFLAGS='$(ASAN)' UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
		./t/run.sh

# `make test-asan` leaves sanitizer-instrumented binaries behind, and valgrind
# refuses to run those, so force a clean build of the CLI first.
test-valgrind:
	rm -f vtparse
	$(MAKE) vtparse
# mktemp rather than a fixed /tmp/vtparse-fuzz.bin: the fixed name meant two
# concurrent runs fed each other's random bytes to valgrind and neither could
# say which input a failure came from, and the file was left behind either
# way. -t honours $TMPDIR.
	@f=$$(mktemp -t vtparse-fuzz.XXXXXXXX) || exit 1; \
	head -c 400000 /dev/urandom > "$$f"; \
	valgrind --error-exitcode=9 --leak-check=full -q \
		./vtparse -u "$$f" > /dev/null; \
	rc=$$?; rm -f "$$f"; exit $$rc
	@echo "valgrind: clean"

# Regenerates width_table.h from the Unicode Character Database.  Deliberately
# NOT a dependency of the build: the generated header is committed so that
# building vtparse needs gcc and nothing else, and the UCD is not in this
# repository.  Re-running this changes the Unicode version and therefore the
# wcwidth disagreement list quoted in README.md.  Set VTPARSE_UCD to point at
# a UCD directory other than the two the generators look in by default.
gen-width:
	python3 tools/gen_width.py width_table.h

# Same deal for the UAX #29 tables.  Both generators read the same UCD and
# both print the version into their header; they must agree, because one
# library with two Unicode versions in it is the bug this pair was written to
# avoid.
gen-grapheme:
	python3 tools/gen_grapheme.py grapheme_table.h

clean:
	rm -f vtparse t/table_test t/corpus_test t/width_diff t/utf8_dump \
		t/grapheme_test t/cli.got t/width_diff.out
