/* table_test.c -- ORACLE 1: the published DEC/ANSI transition table.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is an INDEPENDENT transcription of the table at
 * <https://vt100.net/emu/dec_ansi_parser>.  It is written as declarative range
 * lists that can be read side by side with the published diagram ("00-17,19,
 * 1C-1F / execute"), deliberately in a different form from the if/switch chain
 * in vtparse.c, so that a typo in one is unlikely to be mirrored in the other.
 *
 * It then makes three assertions:
 *   1. the spec table is TOTAL -- every one of the 14*256 cells is defined;
 *   2. vtparse_lookup() agrees with it on all 14*256 (state, byte) pairs,
 *      action, target state and transition-vs-stay alike;
 *   3. the LIVE parser, driven into each state by a real byte prefix and then
 *      fed the byte, lands in the state the table predicts and emits the
 *      callback the table predicts.  (2) checks the table; (3) checks that the
 *      engine actually obeys the table, which is the part a table-only test
 *      would miss.
 */
#include "../vtparse.h"
#include "tap.h"
#include <string.h>

#define STAY (-1)

struct rule {
	const char *ranges;	/* "00-17,19,1C-1F", exactly as published */
	vtparse_action action;
	int next;		/* target state, or STAY for no transition */
};

#define A(x) VTPARSE_ACTION_##x
#define S(x) VTPARSE_##x

/* The "anywhere" column.  Checked before the per-state rules in every state. */
static const struct rule anywhere[] = {
	{"18,1A",		A(EXECUTE),	S(GROUND)},
	{"1B",			A(NONE),	S(ESCAPE)},
	{"80-8F,91-97,99,9A",	A(EXECUTE),	S(GROUND)},
	{"9C",			A(NONE),	S(GROUND)},
	{"90",			A(NONE),	S(DCS_ENTRY)},
	{"98,9E,9F",		A(NONE),	S(SOS_PM_APC_STRING)},
	{"9B",			A(NONE),	S(CSI_ENTRY)},
	{"9D",			A(NONE),	S(OSC_STRING)},
	{NULL, 0, 0}
};

static const struct rule r_ground[] = {
	{"00-17,19,1C-1F",	A(EXECUTE),	STAY},
	{"20-7F",		A(PRINT),	STAY},
	{NULL, 0, 0}
};
static const struct rule r_escape[] = {	/* entry: clear */
	{"00-17,19,1C-1F",	A(EXECUTE),	STAY},
	{"7F",			A(IGNORE),	STAY},
	{"20-2F",		A(COLLECT),	S(ESCAPE_INTERMEDIATE)},
	{"30-4F,51-57,59,5A,5C,60-7E", A(ESC_DISPATCH), S(GROUND)},
	{"50",			A(NONE),	S(DCS_ENTRY)},
	{"58,5E,5F",		A(NONE),	S(SOS_PM_APC_STRING)},
	{"5B",			A(NONE),	S(CSI_ENTRY)},
	{"5D",			A(NONE),	S(OSC_STRING)},
	{NULL, 0, 0}
};
static const struct rule r_escape_intermediate[] = {
	{"00-17,19,1C-1F",	A(EXECUTE),	STAY},
	{"20-2F",		A(COLLECT),	STAY},
	{"7F",			A(IGNORE),	STAY},
	{"30-7E",		A(ESC_DISPATCH), S(GROUND)},
	{NULL, 0, 0}
};
static const struct rule r_csi_entry[] = {	/* entry: clear */
	{"00-17,19,1C-1F",	A(EXECUTE),	STAY},
	{"7F",			A(IGNORE),	STAY},
	{"20-2F",		A(COLLECT),	S(CSI_INTERMEDIATE)},
	{"3A",			A(NONE),	S(CSI_IGNORE)},
	{"30-39,3B",		A(PARAM),	S(CSI_PARAM)},
	{"3C-3F",		A(COLLECT),	S(CSI_PARAM)},
	{"40-7E",		A(CSI_DISPATCH), S(GROUND)},
	{NULL, 0, 0}
};
static const struct rule r_csi_param[] = {
	{"00-17,19,1C-1F",	A(EXECUTE),	STAY},
	{"30-39,3B",		A(PARAM),	STAY},
	{"7F",			A(IGNORE),	STAY},
	{"3A,3C-3F",		A(NONE),	S(CSI_IGNORE)},
	{"20-2F",		A(COLLECT),	S(CSI_INTERMEDIATE)},
	{"40-7E",		A(CSI_DISPATCH), S(GROUND)},
	{NULL, 0, 0}
};
static const struct rule r_csi_intermediate[] = {
	{"00-17,19,1C-1F",	A(EXECUTE),	STAY},
	{"20-2F",		A(COLLECT),	STAY},
	{"7F",			A(IGNORE),	STAY},
	{"30-3F",		A(NONE),	S(CSI_IGNORE)},
	{"40-7E",		A(CSI_DISPATCH), S(GROUND)},
	{NULL, 0, 0}
};
static const struct rule r_csi_ignore[] = {
	{"00-17,19,1C-1F",	A(EXECUTE),	STAY},
	{"20-3F,7F",		A(IGNORE),	STAY},
	{"40-7E",		A(NONE),	S(GROUND)},
	{NULL, 0, 0}
};
static const struct rule r_dcs_entry[] = {	/* entry: clear */
	{"00-17,19,1C-1F",	A(IGNORE),	STAY},
	{"7F",			A(IGNORE),	STAY},
	{"3A",			A(NONE),	S(DCS_IGNORE)},
	{"20-2F",		A(COLLECT),	S(DCS_INTERMEDIATE)},
	{"30-39,3B",		A(PARAM),	S(DCS_PARAM)},
	{"3C-3F",		A(COLLECT),	S(DCS_PARAM)},
	{"40-7E",		A(NONE),	S(DCS_PASSTHROUGH)},
	{NULL, 0, 0}
};
static const struct rule r_dcs_param[] = {
	{"00-17,19,1C-1F",	A(IGNORE),	STAY},
	{"30-39,3B",		A(PARAM),	STAY},
	{"7F",			A(IGNORE),	STAY},
	{"3A,3C-3F",		A(NONE),	S(DCS_IGNORE)},
	{"20-2F",		A(COLLECT),	S(DCS_INTERMEDIATE)},
	{"40-7E",		A(NONE),	S(DCS_PASSTHROUGH)},
	{NULL, 0, 0}
};
static const struct rule r_dcs_intermediate[] = {
	{"00-17,19,1C-1F",	A(IGNORE),	STAY},
	{"20-2F",		A(COLLECT),	STAY},
	{"7F",			A(IGNORE),	STAY},
	{"30-3F",		A(NONE),	S(DCS_IGNORE)},
	{"40-7E",		A(NONE),	S(DCS_PASSTHROUGH)},
	{NULL, 0, 0}
};
static const struct rule r_dcs_passthrough[] = {	/* entry: hook, exit: unhook */
	{"00-17,19,1C-1F",	A(PUT),		STAY},
	{"20-7E",		A(PUT),		STAY},
	{"7F",			A(IGNORE),	STAY},
	{NULL, 0, 0}
};
static const struct rule r_dcs_ignore[] = {
	{"00-17,19,1C-1F",	A(IGNORE),	STAY},
	{"20-7F",		A(IGNORE),	STAY},
	{NULL, 0, 0}
};
static const struct rule r_osc_string[] = {	/* entry: osc_start, exit: osc_end */
	{"00-17,19,1C-1F",	A(IGNORE),	STAY},
	{"20-7F",		A(OSC_PUT),	STAY},
	{NULL, 0, 0}
};
static const struct rule r_sos_pm_apc[] = {
	{"00-17,19,1C-1F",	A(IGNORE),	STAY},
	{"20-7F",		A(IGNORE),	STAY},
	{NULL, 0, 0}
};

static const struct rule *const spec[VTPARSE_NSTATES] = {
	r_ground, r_escape, r_escape_intermediate, r_csi_entry, r_csi_param,
	r_csi_intermediate, r_csi_ignore, r_dcs_entry, r_dcs_param,
	r_dcs_intermediate, r_dcs_passthrough, r_dcs_ignore, r_osc_string,
	r_sos_pm_apc,
};

/* The byte prefix that drives a real parser into each state. */
static const char *const prefix[VTPARSE_NSTATES] = {
	"", "\033", "\033 ", "\033[", "\033[1", "\033[ ", "\033[:",
	"\033P", "\033P1", "\033P ", "\033Pq", "\033P:", "\033]", "\033^",
};

static int hex2(const char *s)
{
	int v = 0, i;
	for (i = 0; i < 2; i++) {
		char c = s[i];
		v = v * 16 + (c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
	}
	return v;
}

/* "00-17,19,1C-1F" -- does it contain b? */
static int ranges_have(const char *s, int b)
{
	while (*s) {
		int lo = hex2(s), hi = lo;
		s += 2;
		if (*s == '-') {
			hi = hex2(s + 1);
			s += 3;
		}
		if (b >= lo && b <= hi)
			return 1;
		if (*s == ',')
			s++;
	}
	return 0;
}

/* Returns 1 if a cell exists, filling act and nxt (nxt == STAY: no move). */
static int spec_lookup(int st, int b, vtparse_action *act, int *nxt)
{
	const struct rule *r;
	for (r = anywhere; r->ranges; r++)
		if (ranges_have(r->ranges, b)) {
			*act = r->action;
			*nxt = r->next;
			return 1;
		}
	/* "codes A0-FF (GR area) are treated identically to codes 20-7F". */
	if (b >= 0xA0)
		b -= 0x80;
	for (r = spec[st]; r->ranges; r++)
		if (ranges_have(r->ranges, b)) {
			*act = r->action;
			*nxt = r->next;
			return 1;
		}
	return 0;
}

/* ---- recorder for part 3 ------------------------------------------------ */

static vtparse_action seen[8];
static int nseen;

static void rec(vtparse *p, vtparse_action a, uint32_t ch)
{
	(void)p;
	(void)ch;
	if (nseen < (int)(sizeof(seen) / sizeof(seen[0])))
		seen[nseen++] = a;
}

static int is_visible(vtparse_action a)
{
	return a >= VTPARSE_ACTION_PRINT;
}

/* Entry/exit actions surround the cell's own action, so a cell whose action is
 * internal may still legitimately produce these. */
static int is_entry_exit(vtparse_action a)
{
	return a == VTPARSE_ACTION_HOOK || a == VTPARSE_ACTION_UNHOOK ||
	       a == VTPARSE_ACTION_OSC_START || a == VTPARSE_ACTION_OSC_END;
}

int main(void)
{
	int st, b;
	int pairs = 0, total_ok = 0, table_ok = 0, live_state_ok = 0,
	    live_action_ok = 0;

	for (st = 0; st < VTPARSE_NSTATES; st++) {
		/* Make sure the prefix really reaches the state it claims to;
		 * otherwise part 3 would be silently testing the wrong state. */
		vtparse p;
		vtparse_init(&p, NULL, NULL, 0);
		vtparse_push(&p, (const unsigned char *)prefix[st],
			     strlen(prefix[st]));
		OK(p.state == (vtparse_state)st, "prefix %d reaches %s (got %s)",
		   st, vtparse_state_name(st), vtparse_state_name(p.state));
	}

	for (st = 0; st < VTPARSE_NSTATES; st++) {
		for (b = 0; b < 256; b++) {
			vtparse_action sa, la;
			int sn, trans, expect;
			vtparse_state ln;
			vtparse p;

			pairs++;
			if (!spec_lookup(st, b, &sa, &sn)) {
				continue;	/* counted as a total_ok miss */
			}
			total_ok++;

			vtparse_lookup((vtparse_state)st, (unsigned char)b,
				       &la, &ln, &trans);
			if (la == sa &&
			    ((sn == STAY && !trans && ln == (vtparse_state)st) ||
			     (sn != STAY && trans && ln == (vtparse_state)sn)))
				table_ok++;
			else
				printf("# table mismatch %s + %02X: spec %s->%s"
				       " got %s->%s%s\n",
				       vtparse_state_name(st), b,
				       vtparse_action_name(sa),
				       sn == STAY ? "(stay)"
						  : vtparse_state_name(sn),
				       vtparse_action_name(la),
				       vtparse_state_name(ln),
				       trans ? "" : " (stay)");

			/* Part 3: the live parser. */
			nseen = 0;
			vtparse_init(&p, rec, NULL, 0);
			vtparse_push(&p, (const unsigned char *)prefix[st],
				     strlen(prefix[st]));
			nseen = 0;
			vtparse_push_byte(&p, (unsigned char)b);
			expect = (sn == STAY) ? st : sn;
			if ((int)p.state == expect)
				live_state_ok++;
			else
				printf("# live mismatch %s + %02X: want %s"
				       " got %s\n", vtparse_state_name(st), b,
				       vtparse_state_name(expect),
				       vtparse_state_name(p.state));
			if (is_visible(sa)) {
				int i, found = 0;
				for (i = 0; i < nseen; i++)
					if (seen[i] == sa)
						found = 1;
				if (found)
					live_action_ok++;
				else
					printf("# live action %s + %02X:"
					       " no %s emitted\n",
					       vtparse_state_name(st), b,
					       vtparse_action_name(sa));
			} else {
				int i, clean = 1;
				for (i = 0; i < nseen; i++)
					if (!is_entry_exit(seen[i]))
						clean = 0;
				if (clean)
					live_action_ok++;
				else
					printf("# live action %s + %02X:"
					       " unexpected callback\n",
					       vtparse_state_name(st), b);
			}
		}
	}

	OK(pairs == VTPARSE_NSTATES * 256, "%d (state,byte) pairs enumerated"
	   " (%d states x 256)", pairs, VTPARSE_NSTATES);
	OK(total_ok == pairs, "published table is total: %d/%d cells defined",
	   total_ok, pairs);
	OK(table_ok == pairs, "vtparse_lookup matches the published table:"
	   " %d/%d pairs", table_ok, pairs);
	OK(live_state_ok == pairs, "live parser state transitions match:"
	   " %d/%d pairs", live_state_ok, pairs);
	OK(live_action_ok == pairs, "live parser callbacks match: %d/%d pairs",
	   live_action_ok, pairs);
	return tap_fail;
}
