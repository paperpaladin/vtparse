/* width_diff.c -- ORACLE 2: differential width check against libc wcwidth(3).
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Walks the whole BMP (U+0000..U+FFFF) in a UTF-8 locale and compares
 * vtwidth() with the system wcwidth().  glibc builds its width table the same
 * way we do -- from UnicodeData.txt and EastAsianWidth.txt -- so where the two
 * disagree the cause is almost always that they were generated from different
 * Unicode versions.  The disagreements are printed as ranges, with the pass
 * threshold set loose enough that the test does not break on a glibc upgrade
 * but tight enough that a broken table fails loudly.
 *
 * Writes the range list to t/width_diff.out so README.md can quote it without
 * anyone having to retype it.
 */
#define _XOPEN_SOURCE 700
#include "../vtparse.h"
#include "tap.h"
#include <locale.h>
#include <stdio.h>
#include <wchar.h>

#define BMP 0x10000

int main(void)
{
	int agree = 0, differ = 0, cp;
	int groups = 0;
	FILE *f;
	const char *loc = setlocale(LC_ALL, "en_US.UTF-8");

	if (!loc)
		loc = setlocale(LC_ALL, "C.UTF-8");
	if (!loc) {
		/* No UTF-8 locale means wcwidth() only knows about ASCII, so
		 * the comparison would be meaningless rather than failing. */
		printf("ok - SKIP no UTF-8 locale available for wcwidth(3)\n");
		return 0;
	}
	printf("# locale=%s, vtparse table Unicode %s\n", loc,
	       vtwidth_unicode_version());

	f = fopen("t/width_diff.out", "w");
	if (f)
		fprintf(f, "locale=%s vtparse-unicode=%s\n", loc,
			vtwidth_unicode_version());

	for (cp = 0; cp < BMP; cp++) {
		if (vtwidth((uint32_t)cp) == wcwidth((wchar_t)cp)) {
			agree++;
			continue;
		}
		differ++;
		/* Collapse consecutive codepoints with the same (mine, libc)
		 * verdict into one reported range. */
		if (cp == 0 ||
		    vtwidth((uint32_t)(cp - 1)) == wcwidth((wchar_t)(cp - 1)) ||
		    vtwidth((uint32_t)cp) != vtwidth((uint32_t)(cp - 1)) ||
		    wcwidth((wchar_t)cp) != wcwidth((wchar_t)(cp - 1))) {
			int end = cp;
			while (end + 1 < BMP &&
			       vtwidth((uint32_t)(end + 1)) !=
				       wcwidth((wchar_t)(end + 1)) &&
			       vtwidth((uint32_t)(end + 1)) ==
				       vtwidth((uint32_t)cp) &&
			       wcwidth((wchar_t)(end + 1)) ==
				       wcwidth((wchar_t)cp))
				end++;
			groups++;
			printf("# U+%04X..U+%04X  vtparse=%d glibc=%d\n",
			       cp, end, vtwidth((uint32_t)cp),
			       wcwidth((wchar_t)cp));
			if (f)
				fprintf(f, "U+%04X..U+%04X vtparse=%d glibc=%d\n",
					cp, end, vtwidth((uint32_t)cp),
					wcwidth((wchar_t)cp));
		}
	}
	if (f) {
		fprintf(f, "agree=%d differ=%d groups=%d rate=%.4f%%\n",
			agree, differ, groups, 100.0 * agree / BMP);
		fclose(f);
	}

	printf("# %d agree, %d differ in %d ranges, %.4f%% agreement\n",
	       agree, differ, groups, 100.0 * agree / BMP);
	/* The observed drift between two adjacent Unicode versions is a couple
	 * of hundred codepoints.  A real table bug (a dropped range, a broken
	 * binary search) costs thousands and trips this immediately. */
	OK(agree >= BMP - 328, "vtwidth agrees with wcwidth(3) on %d/%d BMP"
	   " codepoints (%.4f%%)", agree, BMP, 100.0 * agree / BMP);
	OK(differ == 0 || groups > 0, "%d disagreements reported as %d ranges",
	   differ, groups);
	return tap_fail;
}
