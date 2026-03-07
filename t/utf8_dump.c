/* utf8_dump.c -- helper for ORACLE 3.  Reads hex byte strings, one per line,
 * and prints the codepoints our decoder produces for each, space separated.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * One process for the whole corpus: a subprocess per test case would turn a
 * 90,000-case differential into a coffee break.  The decoder is reset per line,
 * so the lines are independent inputs, not one stream.
 */
#include "../vtparse.h"
#include <stdio.h>
#include <stdlib.h>

static int unhex(int c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

int main(void)
{
	char line[4096];

	while (fgets(line, sizeof(line), stdin)) {
		vtutf8 d;
		uint32_t cp;
		int i = 0, first = 1;

		vtutf8_init(&d);
		for (;;) {
			int hi = unhex((unsigned char)line[i]);
			int lo, r;
			unsigned char b;
			if (hi < 0)
				break;
			lo = unhex((unsigned char)line[i + 1]);
			if (lo < 0) {
				fprintf(stderr, "odd hex digit count\n");
				return 2;
			}
			i += 2;
			b = (unsigned char)(hi * 16 + lo);
			for (;;) {
				r = vtutf8_push(&d, b, &cp);
				if (r == VTUTF8_MORE)
					break;
				printf("%s%04X", first ? "" : " ", cp);
				first = 0;
				if (r == VTUTF8_CP)
					break;
				/* VTUTF8_REPLACE: re-push the same byte. */
			}
		}
		if (vtutf8_flush(&d, &cp)) {
			printf("%s%04X", first ? "" : " ", cp);
			first = 0;
		}
		putchar('\n');
	}
	return 0;
}
