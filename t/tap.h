/* tap.h -- three-line test reporting, shared by the C test programs.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * t/run.sh counts the "ok"/"not ok" lines, so the format is the contract.
 */
#ifndef TAP_H
#define TAP_H
#include <stdio.h>

static int tap_fail;

#define OK(cond, ...)                                                         \
	do {                                                                  \
		int ok_ = !!(cond);                                            \
		printf("%sok - ", ok_ ? "" : "not ");                          \
		printf(__VA_ARGS__);                                           \
		putchar('\n');                                                 \
		if (!ok_)                                                      \
			tap_fail = 1;                                          \
	} while (0)

#endif
