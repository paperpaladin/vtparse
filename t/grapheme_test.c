/* grapheme_test.c -- ORACLE 5: UAX #29 against GraphemeBreakTest.txt.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GraphemeBreakTest.txt is the Unicode Consortium's own conformance file for
 * the grapheme cluster rules: every case is a run of code points with every
 * boundary marked as a break or a non-break, AND with the rule number that
 * decided it.  It was written by the people who wrote the rules, which makes
 * it a stronger authority than anything else in this repository, including
 * the DEC table.
 *
 * The rule numbers are the reason this file reports per rule instead of
 * printing one total.  A grapheme test over ASCII passes with no
 * implementation at all -- break everywhere is correct for every pair of
 * Latin letters -- so a bare "853/853" says nothing about whether GB11
 * (emoji ZWJ sequences), GB12/GB13 (flags), GB6/GB7/GB8 (Hangul) or GB9c
 * (Indic conjuncts) were ever reached.  Tallying the decisions by the rule
 * the file names makes that visible, and a rule that drops to zero decisions
 * is a defect even when nothing fails.
 *
 * The UCD is not in this repository (it lives on a host mount here), so the
 * corpus can be absent.  When it is, this prints a SKIP and says so loudly,
 * and the exhaustive single-code-point check below still runs: that one needs
 * no corpus, and it ties vtwidth_cluster() back to vtwidth(), which does have
 * an external oracle.
 */
#include "../vtparse.h"
#include "tap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXCP   64
#define MAXRULE 32

static const char *DEFAULT_PATH =
	"/run/host/usr/share/unicode/ucd/auxiliary/GraphemeBreakTest.txt";

/* The marks are U+00F7 DIVISION SIGN and U+00D7 MULTIPLICATION SIGN, written
 * here as their UTF-8 bytes so this file stays ASCII. */
static const char DIV[] = "\xc3\xb7";
static const char MUL[] = "\xc3\x97";

struct rule {
	char name[8];
	long seen;	/* boundaries this rule decided */
	long wrong;	/* ... of which we got wrong */
};

static struct rule rules[MAXRULE];
static int nrules;

static struct rule *rule_slot(const char *name)
{
	int i;

	for (i = 0; i < nrules; i++)
		if (!strcmp(rules[i].name, name))
			return &rules[i];
	if (nrules == MAXRULE)
		return NULL;
	snprintf(rules[nrules].name, sizeof rules[nrules].name, "%s", name);
	return &rules[nrules++];
}

/* Pull the "[9.3]" tokens out of the trailing comment, in order.  They line
 * up one for one with the marks in the data part of the line. */
static int parse_rules(const char *comment, char out[][8], int max)
{
	int n = 0;
	const char *p = comment;

	while (n < max && (p = strchr(p, '[')) != NULL) {
		const char *q = strchr(p, ']');
		size_t len;

		if (!q)
			break;
		len = (size_t)(q - p - 1);
		if (len == 0 || len >= 8) {
			p = q + 1;
			continue;
		}
		memcpy(out[n], p + 1, len);
		out[n][len] = '\0';
		n++;
		p = q + 1;
	}
	return n;
}

struct totals {
	long cases, pass, lines_bad, eot_bad, overlong;
};

static void run_case(const char *data, const char *comment, struct totals *t,
		     int *reported)
{
	uint32_t cp[MAXCP];
	int brk[MAXCP + 1];		/* brk[i] = break before cp[i] */
	char rname[MAXCP + 1][8];
	int ncp = 0, nmark = 0, nrule, i, bad = 0;
	const char *p = data;
	vtgb g;

	while (*p) {
		while (*p == ' ' || *p == '\t')
			p++;
		if (!*p)
			break;
		if (!strncmp(p, DIV, 2) || !strncmp(p, MUL, 2)) {
			if (nmark > MAXCP) {
				t->overlong++;
				return;
			}
			brk[nmark++] = !strncmp(p, DIV, 2);
			p += 2;
		} else if ((*p >= '0' && *p <= '9') ||
			   (*p >= 'A' && *p <= 'F') ||
			   (*p >= 'a' && *p <= 'f')) {
			char *end;
			unsigned long v = strtoul(p, &end, 16);

			if (ncp == MAXCP) {
				t->overlong++;
				return;
			}
			cp[ncp++] = (uint32_t)v;
			p = end;
		} else {
			t->lines_bad++;
			return;
		}
	}
	/* Every case is mark cp mark cp ... mark, so the counts are fixed.
	 * Checking that here is what stops a parser bug from quietly turning
	 * this oracle into a shorter one. */
	if (nmark != ncp + 1 || ncp == 0) {
		t->lines_bad++;
		return;
	}
	if (!brk[ncp])
		t->eot_bad++;		/* GB2: eot is always a break */

	nrule = parse_rules(comment, rname, MAXCP + 1);
	t->cases++;

	vtgb_init(&g);
	for (i = 0; i < ncp; i++) {
		int got = vtgb_break(&g, cp[i]);
		struct rule *r = (i < nrule) ? rule_slot(rname[i]) : NULL;

		if (r)
			r->seen++;
		if (got == brk[i])
			continue;
		bad = 1;
		if (r)
			r->wrong++;
		if (*reported < 20) {
			printf("# mismatch before U+%04X (rule %s): file says"
			       " %s, we say %s\n   #   %s\n", cp[i],
			       (i < nrule) ? rname[i] : "?",
			       brk[i] ? "break" : "no break",
			       got ? "break" : "no break", data);
			(*reported)++;
		}
	}
	if (!bad)
		t->pass++;

	/* The array entry point must segment the same text the same way as
	 * the incremental one, or the two halves of the API disagree and
	 * only one of them is being checked here. */
	{
		size_t off = 0;
		int k = 0;

		while (off < (size_t)ncp) {
			size_t len = 0;
			vtwidth_cluster(cp + off, (size_t)ncp - off, &len);
			if (len == 0) {
				printf("# vtwidth_cluster returned a zero-length"
				       " cluster: %s\n", data);
				t->lines_bad++;
				return;
			}
			off += len;
			k++;
			if (off < (size_t)ncp && !brk[off]) {
				printf("# vtwidth_cluster split at offset %zu"
				       " where the file says no break: %s\n",
				       off, data);
				t->lines_bad++;
				return;
			}
		}
		(void)k;
	}
}

static int conformance(const char *path)
{
	char line[4096];
	struct totals t;
	FILE *f = fopen(path, "r");
	int reported = 0, i;
	long decided = 0, wrong = 0;

	memset(&t, 0, sizeof t);
	if (!f) {
		printf("ok - SKIP grapheme conformance: no GraphemeBreakTest.txt"
		       " at %s (set VTPARSE_UCD or pass a path)\n", path);
		printf("# NOTHING WAS CHECKED against UAX #29 on this run.\n");
		return 0;
	}
	while (fgets(line, sizeof line, f)) {
		char *hash = strchr(line, '#');
		/* comment must be taken as hash + 1: the NUL below lands on
		 * the hash itself, so pointing at it yields an empty string
		 * and every rule tally silently reads zero.  That is exactly
		 * what the first run of this file did, and it reported
		 * "853/853 cases, 0/0 boundaries" without complaint. */
		const char *comment = hash ? hash + 1 : "";

		if (hash)
			*hash = '\0';
		if (!strncmp(line, "@", 1))
			continue;
		if (strspn(line, " \t\r\n") == strlen(line))
			continue;
		run_case(line, comment, &t, &reported);
	}
	fclose(f);

	printf("# corpus %s, vtparse grapheme table Unicode %s\n", path,
	       vtwidth_unicode_version());
	for (i = 0; i < nrules; i++) {
		decided += rules[i].seen;
		wrong += rules[i].wrong;
		printf("# rule %-6s %6ld boundaries, %ld wrong\n",
		       rules[i].name, rules[i].seen, rules[i].wrong);
	}
	OK(t.lines_bad == 0, "every line of the corpus parsed (%ld rejected)",
	   t.lines_bad);
	OK(t.eot_bad == 0, "every case ends in a break, as GB2 says (%ld did"
	   " not)", t.eot_bad);
	OK(t.overlong == 0, "no case exceeded the %d code point buffer (%ld"
	   " did)", MAXCP, t.overlong);
	OK(t.cases > 0 && t.pass == t.cases,
	   "GraphemeBreakTest.txt: %ld/%ld cases, %ld/%ld boundaries",
	   t.pass, t.cases, decided - wrong, decided);

	/* The rules that are not exercised by ASCII.  Naming them here means
	 * a corpus that stopped covering one of them fails rather than
	 * quietly reporting a smaller, easier number. */
	{
		static const char *const hard[] = {
			"3.0",	/* CR LF */
			"6.0", "7.0", "8.0",	/* Hangul */
			"9.0",	/* combining mark after a base */
			"9.1", "9.2",		/* SpacingMark, Prepend */
			"9.3",	/* Indic conjunct, GB9c */
			"11.0",	/* emoji ZWJ sequence */
			"12.0", "13.0",		/* regional indicator pairs */
		};
		size_t h;

		for (h = 0; h < sizeof hard / sizeof hard[0]; h++) {
			struct rule *r = rule_slot(hard[h]);
			OK(r && r->seen > 0 && r->wrong == 0,
			   "rule %s decided %ld boundaries, %ld wrong",
			   hard[h], r ? r->seen : 0, r ? r->wrong : 0);
		}
	}
	return 0;
}

/* vtwidth_cluster() over a single code point must be vtwidth() of it, for
 * every code point there is.  vtwidth() is checked against glibc's
 * wcwidth(3), so this is the one part of the cluster width convention that
 * inherits an external oracle, and it needs no corpus. */
static void single_codepoint_identity(void)
{
	uint32_t cp;
	long bad = 0, badlen = 0;
	uint32_t first_bad = 0;

	for (cp = 0; cp <= 0x10FFFF; cp++) {
		size_t len = 0;
		int w = vtwidth_cluster(&cp, 1, &len);

		if (len != 1)
			badlen++;
		if (w != vtwidth(cp)) {
			if (!bad)
				first_bad = cp;
			bad++;
		}
	}
	OK(bad == 0, "vtwidth_cluster of one code point == vtwidth, all"
	   " 1114112 of them (%ld differ, first U+%04X)", bad, first_bad);
	OK(badlen == 0, "a one code point cluster has length 1, all 1114112"
	   " of them (%ld did not)", badlen);
}

/* The width convention has no external oracle -- UAX #29 says where clusters
 * end and says nothing about columns -- so these are printed, not asserted.
 * See README. */
static void width_demo(void)
{
	static const uint32_t flag[]   = {0x1F1EF, 0x1F1F5};
	static const uint32_t family[] = {0x1F468, 0x200D, 0x1F469, 0x200D,
					  0x1F467};
	static const uint32_t keycap[] = {0x0023, 0xFE0F, 0x20E3};
	static const uint32_t devan[]  = {0x0915, 0x094D, 0x0937, 0x093F};
	static const uint32_t acc[]    = {0x0065, 0x0301};
	size_t len = 0;
	int w;

	/* One call per printf argument: `printf("%d %zu", f(&len), len)` has
	 * unspecified argument evaluation order and read the old `len`. */
	w = vtwidth_cluster(flag, 2, &len);
	printf("# cluster widths (convention, no oracle): flag=%d(%zu cp)", w, len);
	w = vtwidth_cluster(family, 5, &len);
	printf(" family=%d(%zu cp)", w, len);
	w = vtwidth_cluster(keycap, 3, &len);
	printf(" keycap=%d(%zu cp)", w, len);
	w = vtwidth_cluster(devan, 4, &len);
	printf(" ksi=%d(%zu cp)", w, len);
	w = vtwidth_cluster(acc, 2, &len);
	printf(" e-acute=%d(%zu cp)\n", w, len);
}

int main(int argc, char **argv)
{
	const char *path = DEFAULT_PATH;
	char buf[512];
	const char *ucd = getenv("VTPARSE_UCD");

	if (argc > 1) {
		path = argv[1];
	} else if (ucd) {
		snprintf(buf, sizeof buf,
			 "%s/auxiliary/GraphemeBreakTest.txt", ucd);
		path = buf;
	}
	conformance(path);
	single_codepoint_identity();
	width_demo();
	return tap_fail;
}
