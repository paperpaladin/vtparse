/* main.c -- CLI front end: read bytes, print the decoded event stream.
 *
 * Copyright (C) 2026  PLACEHOLDER
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This exists because a push parser with callbacks is otherwise untestable from
 * a shell, and "run it and diff the output" is the cheapest correctness harness
 * there is.  One line per event, nothing clever.
 */
#include "vtparse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const c0names[0x21] = {
	"NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL",
	"BS", "HT", "LF", "VT", "FF", "CR", "SO", "SI",
	"DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB",
	"CAN", "EM", "SUB", "ESC", "FS", "GS", "RS", "US", "SP",
};

static const char *ctlname(uint32_t c)
{
	if (c <= 0x20)
		return c0names[c];
	if (c == 0x7F)
		return "DEL";
	switch (c) {		/* the C1 controls a terminal actually sees */
	case 0x84: return "IND";
	case 0x85: return "NEL";
	case 0x88: return "HTS";
	case 0x8D: return "RI";
	case 0x8E: return "SS2";
	case 0x8F: return "SS3";
	case 0x90: return "DCS";
	case 0x96: return "SPA";
	case 0x97: return "EPA";
	case 0x9B: return "CSI";
	case 0x9C: return "ST";
	case 0x9D: return "OSC";
	}
	return "C1";
}

static void quote(const char *s, size_t n)
{
	size_t i;
	putchar('"');
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c == '"' || c == '\\')
			printf("\\%c", c);
		else if (c >= 0x20 && c < 0x7F)
			putchar(c);
		else
			printf("\\x%02X", c);
	}
	putchar('"');
}

static void seq_detail(vtparse *p)
{
	int i;
	printf(" inter=");
	quote((const char *)p->intermediates, (size_t)p->nintermediates);
	printf(" params=[");
	for (i = 0; i < p->nparams; i++)
		printf("%s%d", i ? "," : "", p->params[i]);
	printf("]");
	if (p->nparams_seen > p->nparams)
		printf(" +%d-ignored", p->nparams_seen - p->nparams);
	if (p->param_overflow)
		printf(" clamped");
	if (p->intermediate_overflow)
		printf(" inter-overflow");
}

static void on_event(vtparse *p, vtparse_action a, uint32_t ch)
{
	switch (a) {
	case VTPARSE_ACTION_PRINT:
		printf("PRINT U+%04X", ch);
		if (ch >= 0x20 && ch < 0x7F)
			printf(" '%c'", (char)ch);
		printf(" w=%d\n", vtwidth(ch));
		break;
	case VTPARSE_ACTION_EXECUTE:
		printf("EXECUTE 0x%02X %s\n", ch, ctlname(ch));
		break;
	case VTPARSE_ACTION_ESC_DISPATCH:
		printf("ESC_DISPATCH '%c'", (char)ch);
		printf(" inter=");
		quote((const char *)p->intermediates, (size_t)p->nintermediates);
		putchar('\n');
		break;
	case VTPARSE_ACTION_CSI_DISPATCH:
		printf("CSI_DISPATCH '%c'", (char)ch);
		seq_detail(p);
		putchar('\n');
		break;
	case VTPARSE_ACTION_HOOK:
		printf("HOOK '%c'", (char)ch);
		seq_detail(p);
		putchar('\n');
		break;
	case VTPARSE_ACTION_PUT:
		printf("PUT 0x%02X\n", ch);
		break;
	case VTPARSE_ACTION_UNHOOK:
		printf("UNHOOK payload=");
		quote(p->str, p->strlen);
		printf("%s\n", p->str_truncated ? " truncated" : "");
		break;
	case VTPARSE_ACTION_OSC_START:
		printf("OSC_START\n");
		break;
	case VTPARSE_ACTION_OSC_PUT:
		printf("OSC_PUT 0x%02X\n", ch);
		break;
	case VTPARSE_ACTION_OSC_END:
		printf("OSC_END payload=");
		quote(p->str, p->strlen);
		printf("%s\n", p->str_truncated ? " truncated" : "");
		break;
	default:
		break;		/* internal actions are not reported */
	}
}

static void run(vtparse *p, FILE *f)
{
	unsigned char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		vtparse_push(p, buf, n);
}

int main(int argc, char **argv)
{
	vtparse p;
	int utf8 = 0, i, nfiles = 0;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-u") || !strcmp(argv[i], "--utf8")) {
			utf8 = 1;
			argv[i] = NULL;
		} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
			fprintf(stderr,
				"usage: vtparse [-u] [file...]\n"
				"  -u, --utf8   decode ground-state bytes >= 0x80 as UTF-8\n"
				"Reads stdin when no file is given.  Prints one event per line.\n"
				"Unicode %s width tables.\n",
				vtwidth_unicode_version());
			return 0;
		} else {
			nfiles++;
		}
	}
	vtparse_init(&p, on_event, NULL, utf8);
	if (!nfiles) {
		run(&p, stdin);
	} else {
		for (i = 1; i < argc; i++) {
			FILE *f;
			if (!argv[i])
				continue;
			f = fopen(argv[i], "rb");
			if (!f) {
				perror(argv[i]);
				return 1;
			}
			run(&p, f);
			fclose(f);
		}
	}
	vtparse_flush(&p);
	/* The final state is part of the answer: a truncated escape sequence
	 * leaves the parser mid-sequence on purpose, because the next read()
	 * might complete it.  Printing it makes that visible to a test. */
	printf("EOF state=%s\n", vtparse_state_name(p.state));
	return 0;
}
