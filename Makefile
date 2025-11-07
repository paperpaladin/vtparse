# vtparse -- SPDX-License-Identifier: GPL-3.0-or-later

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -Wpedantic -O2 -g

.PHONY: all clean

all: vtparse

vtparse: main.c vtparse.c vtparse.h
	$(CC) $(CFLAGS) -o $@ main.c vtparse.c

clean:
	rm -f vtparse t/table_test t/corpus_test t/width_diff t/utf8_dump \
		t/cli.got
