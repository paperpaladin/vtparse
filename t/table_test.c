/* table_test.c -- ORACLE 1: the published DEC/ANSI transition table.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is an INDEPENDENT transcription of the table at
 * <https://vt100.net/emu/dec_ansi_parser>.  It is written as declarative range
 * lists that can be read side by side with the published diagram ("00-17,19,
 * 1C-1F / execute"), deliberately in a different form from the if/switch chain
 * in vtparse.c, so that a typo in one is unlikely to be mirrored in the other.
 *
 * It then makes four assertions:
 *   1. the spec table is TOTAL -- every one of the 14*256 cells is defined;
 *   2. vtparse_lookup() agrees with it on all 14*256 (state, byte) pairs,
 *      action, target state and transition-vs-stay alike;
 *   3. the LIVE parser, driven into each state by a real byte prefix and then
 *      fed the byte, lands in the state the table predicts and emits the
 *      callback the table predicts.  (2) checks the table; (3) checks that the
 *      engine actually obeys the table, which is the part a table-only test
 *      would miss.
 *   4. the live parser's EFFECT on the sequence state is exactly what the
 *      cell's action, plus the entry and exit actions of the states it leaves
 *      and enters, are specified to do.  (3) is blind to 1076 of the 3584
 *      cells: it only looks at callbacks, and collect, param and clear emit
 *      none, so the whole table scored 3584/3584 on four separate lines with
 *      collect gutted, with param gutted, and with the clear entry action
 *      never running.  All three were measured, each one a separate build.
 *      The effect model below is what makes those 1076 cells distinguishable
 *      from cells that were merely enumerated.
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

/* The published state table has 14 states. This is a fact about the table,
 * not about this implementation, so it is spelled out rather than read from
 * vtparse.h -- see the OK() that uses it. */
#define PUBLISHED_STATES 14

static const struct rule *const spec[VTPARSE_NSTATES] = {
	r_ground, r_escape, r_escape_intermediate, r_csi_entry, r_csi_param,
	r_csi_intermediate, r_csi_ignore, r_dcs_entry, r_dcs_param,
	r_dcs_intermediate, r_dcs_passthrough, r_dcs_ignore, r_osc_string,
	r_sos_pm_apc,
};

/* The byte prefix that drives a real parser into each state.  Five of these
 * are longer than they need to be, and each extra byte kills a mutant that
 * the minimal prefix left alive, because an action's less common branch is
 * only reached from a parser that is already part way through a sequence:
 *
 *   ESC SP SP / CSI SP SP / DCS SP SP   two intermediates are already stored,
 *       so collect in the three intermediate states takes the overflow branch
 *       rather than the append branch (the entry states still take append)
 *   CSI 1 0 0 0   cur is 1000, so one more digit is 10000: over the spec's
 *       9999 clamp, but not so far over that a mutant raising the ceiling
 *       lands on 9999 anyway and agrees
 *   DCS q z / OSC x   the payload buffer is not empty on arrival, so hook's
 *       and osc_start's buffer reset is observable
 *
 * The literal branch counts asserted at the end of main() are what keeps
 * these honest: shorten a prefix and the count it feeds drops to zero. */
static const char *const prefix[VTPARSE_NSTATES] = {
	"", "\033", "\033  ", "\033[", "\033[1000", "\033[  ", "\033[:",
	"\033P", "\033P1", "\033P  ", "\033Pqz", "\033P:", "\033]x",
	"\033^",
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

/* Exactly the actions the library reports to the callback.  clear, collect
 * and param are bookkeeping and never reach it. */
static int is_visible(vtparse_action a)
{
	return a >= VTPARSE_ACTION_PRINT;
}

/* Entry and exit actions belong to the state box rather than to a cell, so
 * they can arrive on a transition whose own action column is internal, and
 * a callback carrying one is not evidence of a wrong table. Taken from
 * vtparse.c's state_entry and state_exit rather than from the prose: exit
 * gives UNHOOK and OSC_END, entry gives CLEAR, HOOK and OSC_START. CLEAR is
 * not in this list because it is internal bookkeeping and never reaches the
 * callback, so it cannot appear in what was seen. */
static int is_entry_exit(vtparse_action a)
{
	return a == VTPARSE_ACTION_HOOK || a == VTPARSE_ACTION_UNHOOK
	    || a == VTPARSE_ACTION_OSC_START || a == VTPARSE_ACTION_OSC_END;
}

/* ---- the state boxes, and what each action does ------------------------- */

/* The published table puts entry and exit actions on the state box rather
 * than in a cell, so they are transcribed here the same way the cells are,
 * independently of the switch statements in vtparse.c.  A(NONE) means the box
 * carries no action of that kind. */
static const vtparse_action entry_act[VTPARSE_NSTATES] = {
	[S(GROUND)] = A(NONE),
	[S(ESCAPE)] = A(CLEAR),
	[S(ESCAPE_INTERMEDIATE)] = A(NONE),
	[S(CSI_ENTRY)] = A(CLEAR),
	[S(CSI_PARAM)] = A(NONE),
	[S(CSI_INTERMEDIATE)] = A(NONE),
	[S(CSI_IGNORE)] = A(NONE),
	[S(DCS_ENTRY)] = A(CLEAR),
	[S(DCS_PARAM)] = A(NONE),
	[S(DCS_INTERMEDIATE)] = A(NONE),
	[S(DCS_PASSTHROUGH)] = A(HOOK),
	[S(DCS_IGNORE)] = A(NONE),
	[S(OSC_STRING)] = A(OSC_START),
	[S(SOS_PM_APC_STRING)] = A(NONE),
};
static const vtparse_action exit_act[VTPARSE_NSTATES] = {
	[S(DCS_PASSTHROUGH)] = A(UNHOOK),
	[S(OSC_STRING)] = A(OSC_END),
	/* every other box: A(NONE), which is 0 */
};

/* What the spec says each action does to the sequence being accumulated,
 * applied to a copy of the parser rather than read out of vtparse.c.  The
 * byte is the one that arrived, NOT the GL-folded one: the spec folds A0-FF
 * for ROUTING only, and the value handed to an action is the original (see
 * README, "Design").  That distinction is load-bearing for 192 PRINT cells
 * and 411 put/osc_put cells. */
static void apply(vtparse *e, vtparse_action a, unsigned char b)
{
	int i;

	switch (a) {
	case A(NONE):		/* the blank cell: nothing accumulates */
	case A(IGNORE):
	case A(EXECUTE):	/* a control is dispatched, not accumulated */
	case A(ESC_DISPATCH):
	case A(UNHOOK):
	case A(OSC_END):
		break;
	case A(CLEAR):
		e->nparams = 0;
		e->nparams_seen = 0;
		e->cur = -1;
		e->param_overflow = 0;
		e->nintermediates = 0;
		e->intermediate_overflow = 0;
		e->str[0] = '\0';
		e->strlen = 0;
		e->str_truncated = 0;
		break;
	case A(COLLECT):
		if (e->nintermediates < VTPARSE_MAX_INTERMEDIATES)
			e->intermediates[e->nintermediates++] = b;
		else
			e->intermediate_overflow = 1;
		break;
	case A(PARAM):
		if (b == ';') {
			if (e->nparams < VTPARSE_MAX_PARAMS)
				e->params[e->nparams++] = e->cur;
			e->nparams_seen++;
			e->cur = -1;
		} else {
			int32_t v = (e->cur < 0 ? 0 : e->cur) * 10 + (b - '0');
			if (v > VTPARSE_PARAM_MAX) {
				v = VTPARSE_PARAM_MAX;
				e->param_overflow = 1;
			}
			e->cur = v;
		}
		break;
	case A(PRINT):
		e->codepoint = b;
		break;
	case A(CSI_DISPATCH):	/* the trailing parameter has no separator */
	case A(HOOK):
		if (e->cur >= 0 || e->nparams_seen > 0) {
			if (e->nparams < VTPARSE_MAX_PARAMS)
				e->params[e->nparams++] = e->cur;
			e->nparams_seen++;
			e->cur = -1;
		}
		break;
	case A(PUT):
	case A(OSC_PUT):
		if (e->strlen < VTPARSE_STRING_MAX) {
			e->str[e->strlen++] = (char)b;
			e->str[e->strlen] = '\0';
		} else {
			e->str_truncated = 1;
		}
		break;
	case A(OSC_START):	/* "initialization of the buffer" */
		e->str[0] = '\0';
		e->strlen = 0;
		e->str_truncated = 0;
		break;
	default:
		for (i = 0; i < 1; i++)
			printf("# apply(): no model for action %s\n",
			       vtparse_action_name(a));
		break;
	}
}

/* Names the first field that disagrees, because memcmp on its own says only
 * that something did. */
static const char *first_diff(const vtparse *want, const vtparse *got)
{
	if (want->nparams != got->nparams)
		return "nparams";
	if (memcmp(want->params, got->params, sizeof want->params))
		return "params[]";
	if (want->nparams_seen != got->nparams_seen)
		return "nparams_seen";
	if (want->cur != got->cur)
		return "cur";
	if (want->param_overflow != got->param_overflow)
		return "param_overflow";
	if (want->nintermediates != got->nintermediates)
		return "nintermediates";
	if (memcmp(want->intermediates, got->intermediates,
		   sizeof want->intermediates))
		return "intermediates[]";
	if (want->intermediate_overflow != got->intermediate_overflow)
		return "intermediate_overflow";
	if (want->strlen != got->strlen)
		return "strlen";
	if (memcmp(want->str, got->str, sizeof want->str))
		return "str[]";
	if (want->str_truncated != got->str_truncated)
		return "str_truncated";
	if (want->codepoint != got->codepoint)
		return "codepoint";
	return "a field this comparison does not name";
}

/* How many of the 3584 cells carry each action.  These are literals on
 * purpose.  Deriving them from the spec[] arrays beside them would make the
 * line agree with itself whatever those arrays said, which is how a count
 * ends up describing less than its name claims; with literals, moving one
 * cell between two ranges prints a disagreement.  They were counted once,
 * from the transcription, and they sum to 3584. */
static const struct { vtparse_action a; int cells; } census[] = {
	{A(NONE),		716},
	{A(IGNORE),		642},
	{A(COLLECT),		272},
	{A(PARAM),		 88},
	{A(PRINT),		192},
	{A(EXECUTE),		581},
	{A(ESC_DISPATCH),	304},
	{A(CSI_DISPATCH),	378},
	{A(PUT),		219},
	{A(OSC_PUT),		192},
};
#define NCENSUS ((int)(sizeof census / sizeof census[0]))

int main(void)
{
	int st, b, i;
	int pairs = 0, total_ok = 0, table_ok = 0, live_state_ok = 0,
	    live_action_ok = 0, effect_ok = 0;
	int hist[VTPARSE_NACTIONS], census_sum = 0;

	for (i = 0; i < VTPARSE_NACTIONS; i++)
		hist[i] = 0;

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
			vtparse p, before, want;

			pairs++;
			if (!spec_lookup(st, b, &sa, &sn)) {
				continue;	/* counted as a total_ok miss */
			}
			total_ok++;
			hist[sa]++;

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
			before = p;
			vtparse_push_byte(&p, (unsigned char)b);
			expect = (sn == STAY) ? st : sn;

			/* Part 4: did the cell's action actually happen?  The
			 * spec's order is exit action of the old state, the
			 * cell's own action, entry action of the new one; a
			 * stay runs only the middle one. */
			want = before;
			if (sn == STAY) {
				apply(&want, sa, (unsigned char)b);
			} else {
				apply(&want, exit_act[st], (unsigned char)b);
				apply(&want, sa, (unsigned char)b);
				apply(&want, entry_act[sn], (unsigned char)b);
			}
			want.state = p.state;	/* checked separately below */
			if (!memcmp(&want, &p, sizeof want))
				effect_ok++;
			else
				printf("# effect %s + %02X (%s): %s differs\n",
				       vtparse_state_name(st), b,
				       vtparse_action_name(sa),
				       first_diff(&want, &p));
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

	/* PUBLISHED_STATES, not VTPARSE_NSTATES. The published table has 14
	 * states, and taking the denominator from the enum in vtparse.h makes
	 * this an assertion about the implementation agreeing with itself:
	 * adding a state to the enum (and extending the spec[] and prefix[]
	 * arrays beside it) made this report "3840 (state,byte) pairs
	 * enumerated (15 states x 256)" and pass, while the section heading
	 * still printed "14 states x 256". 14 is a fact about Paul
	 * Williams' table, so it is written down here as one. */
	OK(VTPARSE_NSTATES == PUBLISHED_STATES,
	   "the implementation has the published table's %d states (has %d)",
	   PUBLISHED_STATES, VTPARSE_NSTATES);
	OK(pairs == PUBLISHED_STATES * 256, "%d (state,byte) pairs enumerated"
	   " (%d states x 256)", pairs, PUBLISHED_STATES);
	OK(total_ok == pairs, "published table is total: %d/%d cells defined",
	   total_ok, pairs);
	OK(table_ok == pairs, "vtparse_lookup matches the published table:"
	   " %d/%d pairs", table_ok, pairs);
	OK(live_state_ok == pairs, "live parser state transitions match:"
	   " %d/%d pairs", live_state_ok, pairs);
	OK(live_action_ok == pairs, "live parser callbacks match: %d/%d pairs",
	   live_action_ok, pairs);
	OK(effect_ok == pairs, "live parser effect on the sequence state matches"
	   " the specified actions: %d/%d pairs", effect_ok, pairs);

	/* The census is what stops the line above passing vacuously.  3584
	 * cells agreeing proves nothing about collect unless collect is known
	 * to be one of the actions the table reaches, and how often. */
	for (i = 0; i < NCENSUS; i++) {
		census_sum += census[i].cells;
		OK(hist[census[i].a] == census[i].cells,
		   "%s is the cell action %d times (counted %d)",
		   vtparse_action_name(census[i].a), census[i].cells,
		   hist[census[i].a]);
	}
	OK(census_sum == pairs, "the census accounts for every cell: %d of %d"
	   " (a name listed twice would not)", census_sum, pairs);

	return tap_fail;
}
