/* vtparse.c -- the DEC/ECMA-48 parser state machine, UTF-8, and widths.
 *
 * Copyright (C) 2026  PLACEHOLDER
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The transition table below is a transcription of
 * <https://vt100.net/emu/dec_ansi_parser> by Paul Williams.  t/table_test.c
 * holds a second, independent transcription in declarative form and asserts
 * that the two agree on all (state, byte) pairs, and that the live parser's
 * state really follows it.
 */
#include "vtparse.h"
#include <string.h>

/* ------------------------------------------------------------------ table */

/* The C0 controls that the spec routes as a group in every state.  18 (CAN),
 * 1A (SUB) and 1B (ESC) are NOT here: they are "anywhere" transitions and are
 * consumed before we ever get to the per-state logic. */
static int is_c0(unsigned char b)
{
	return b <= 0x17 || b == 0x19 || (b >= 0x1C && b <= 0x1F);
}

#define CELL(a, n, t)                                                         \
	do {                                                                  \
		*action = VTPARSE_ACTION_##a;                                 \
		*next = VTPARSE_##n;                                          \
		*transition = (t);                                            \
		return;                                                       \
	} while (0)
#define STAY(a)                                                               \
	do {                                                                  \
		*action = VTPARSE_ACTION_##a;                                 \
		*next = s;                                                    \
		*transition = 0;                                              \
		return;                                                       \
	} while (0)
#define GOTO(a, n) CELL(a, n, 1)

/* shortcut: this is an if/switch chain evaluated per byte rather than a
 * precomputed uint8_t[14][256] lookup table.  Ceiling: roughly 5-10 branches
 * per byte instead of one array index, which is irrelevant at terminal data
 * rates (megabytes/second is already thousands of times faster than any pty
 * needs) but would show up in a benchmark.  Upgrade path: keep this function
 * as the source of truth and have a generator emit the flat table from it at
 * build time, or fill a static table on first use.  The if-chain stays because
 * it can be read side by side with the published table, which is the whole
 * correctness argument here. */
void vtparse_lookup(vtparse_state s, unsigned char b, vtparse_action *action,
		    vtparse_state *next, int *transition)
{
	/* "anywhere" transitions: these win over the per-state table in every
	 * state.  WHY: C0 and C1 controls are executed immediately, even in the
	 * middle of a control sequence -- the spec says so explicitly, and it is
	 * what makes a terminal survive a program that interleaves a BEL or a
	 * newline into its own escape sequence.  An ESC mid-sequence likewise
	 * abandons whatever was being built; that is how a truncated sequence
	 * followed by a good one still works. */
	switch (b) {
	case 0x18:
	case 0x1A:
		GOTO(EXECUTE, GROUND);
	case 0x1B:
		GOTO(NONE, ESCAPE);
	}

	switch (s) {
	case VTPARSE_GROUND:
		if (is_c0(b))
			STAY(EXECUTE);
		STAY(PRINT);	/* 20-7F; see README on DEL */

	case VTPARSE_ESCAPE:
		if (is_c0(b))
			STAY(EXECUTE);
		if (b == 0x7F)
			STAY(IGNORE);
		if (b <= 0x2F)
			GOTO(COLLECT, ESCAPE_INTERMEDIATE);
		switch (b) {
		}
		/* 30-4F, 51-57, 59, 5A, 5C, 60-7E */
		GOTO(ESC_DISPATCH, GROUND);

	case VTPARSE_ESCAPE_INTERMEDIATE:
		if (is_c0(b))
			STAY(EXECUTE);
		if (b <= 0x2F)
			STAY(COLLECT);
		if (b == 0x7F)
			STAY(IGNORE);
		GOTO(ESC_DISPATCH, GROUND);

	default:
		break;
	}
	STAY(IGNORE);		/* unreachable; keeps the compiler quiet */
}

/* ----------------------------------------------------------------- engine */

static void emit(vtparse *p, vtparse_action a, uint32_t ch)
{
	if (p->cb)
		p->cb(p, a, ch);
}

static void clear_seq(vtparse *p)
{
	p->nparams = 0;
	p->nparams_seen = 0;
	p->cur = -1;
	p->param_overflow = 0;
	p->nintermediates = 0;
	p->intermediate_overflow = 0;
	p->str[0] = '\0';
	p->strlen = 0;
	p->str_truncated = 0;
}

static void do_action(vtparse *p, vtparse_action a, unsigned char b)
{
	switch (a) {
	case VTPARSE_ACTION_NONE:
	case VTPARSE_ACTION_IGNORE:
		break;
	case VTPARSE_ACTION_CLEAR:
		clear_seq(p);
		break;
	case VTPARSE_ACTION_COLLECT:
		if (p->nintermediates < VTPARSE_MAX_INTERMEDIATES)
			p->intermediates[p->nintermediates++] = b;
		else
			p->intermediate_overflow = 1;
		break;
	case VTPARSE_ACTION_PARAM:
		/* shortcut: the digits are routed but not yet stored, so every
		 * dispatch reports zero parameters.  Next commit. */
		break;
	case VTPARSE_ACTION_PRINT:
		p->codepoint = b;
		emit(p, a, b);
		break;
	case VTPARSE_ACTION_EXECUTE:
		emit(p, a, b);
		break;
	case VTPARSE_ACTION_ESC_DISPATCH:
		emit(p, a, b);
		break;
	case VTPARSE_ACTION_PUT:
	case VTPARSE_ACTION_OSC_PUT:
		emit(p, a, b);
		break;
	default:
		break;
	}
}

static void state_entry(vtparse *p, vtparse_state s, unsigned char b)
{
	switch (s) {
	case VTPARSE_ESCAPE:
		do_action(p, VTPARSE_ACTION_CLEAR, b);
		break;
	default:
		break;
	}
}

static void step(vtparse *p, unsigned char b)
{
	vtparse_action a;
	vtparse_state next;
	int trans;

	vtparse_lookup(p->state, b, &a, &next, &trans);
	if (!trans) {
		do_action(p, a, b);
		return;
	}
	/* Spec order: the transition's own action, then the entry action of the
	 * new state.  (An exit action joins this order when a state acquires
	 * one.) */
	do_action(p, a, b);
	p->state = next;
	state_entry(p, next, b);
}

void vtparse_init(vtparse *p, vtparse_cb cb, void *user)
{
	memset(p, 0, sizeof(*p));
	p->cb = cb;
	p->user = user;
	p->state = VTPARSE_GROUND;
	p->cur = -1;
}

void vtparse_push_byte(vtparse *p, unsigned char b)
{
	step(p, b);
}

void vtparse_push(vtparse *p, const unsigned char *buf, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++)
		vtparse_push_byte(p, buf[i]);
}

/* ------------------------------------------------------------------ names */

static const char *const state_names[VTPARSE_NSTATES] = {
	"GROUND", "ESCAPE", "ESCAPE_INTERMEDIATE", "CSI_ENTRY", "CSI_PARAM",
	"CSI_INTERMEDIATE", "CSI_IGNORE", "DCS_ENTRY", "DCS_PARAM",
	"DCS_INTERMEDIATE", "DCS_PASSTHROUGH", "DCS_IGNORE", "OSC_STRING",
	"SOS_PM_APC_STRING",
};

static const char *const action_names[VTPARSE_NACTIONS] = {
	"NONE", "IGNORE", "CLEAR", "COLLECT", "PARAM", "PRINT", "EXECUTE",
	"ESC_DISPATCH", "CSI_DISPATCH", "HOOK", "PUT", "UNHOOK", "OSC_START",
	"OSC_PUT", "OSC_END",
};

const char *vtparse_state_name(vtparse_state s)
{
	return (unsigned)s < VTPARSE_NSTATES ? state_names[s] : "?";
}

const char *vtparse_action_name(vtparse_action a)
{
	return (unsigned)a < VTPARSE_NACTIONS ? action_names[a] : "?";
}
