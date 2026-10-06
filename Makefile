# vtparse -- SPDX-License-Identifier: GPL-3.0-or-later

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g

.PHONY: all test gen-width clean

all: vtparse

vtparse: main.c vtparse.c vtparse.h width_table.h
	$(CC) $(CFLAGS) -o $@ main.c vtparse.c

test: all
	CC='$(CC)' CFLAGS='$(CFLAGS)' ./t/run.sh

# Regenerates width_table.h from the python3 in $PATH.  Deliberately NOT a
# dependency of the build: the generated header is committed so that building
# vtparse needs gcc and nothing else.  Re-running this changes the Unicode
# version and therefore the wcwidth disagreement list quoted in README.md.
gen-width:
	python3 tools/gen_width.py width_table.h

clean:
	rm -f vtparse t/table_test t/corpus_test t/width_diff t/utf8_dump \
		t/cli.got t/width_diff.out
