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
	case 0x90:
		GOTO(NONE, DCS_ENTRY);
	case 0x98:
	case 0x9E:
	case 0x9F:
		GOTO(NONE, SOS_PM_APC_STRING);
	case 0x9B:
		GOTO(NONE, CSI_ENTRY);
	case 0x9C:
		GOTO(NONE, GROUND);
	case 0x9D:
		GOTO(NONE, OSC_STRING);
	}
	/* 80-8F, 91-97, 99, 9A: the remaining C1 controls, all executed.  The
	 * cases above already removed 90, 98, 9B-9F from this range. */
	if (b >= 0x80 && b <= 0x9A)
		GOTO(EXECUTE, GROUND);

	/* "In all cases shown here, codes A0-FF (GR area) are treated
	 * identically to codes 20-7F (GL area)."  So fold and continue. */
	if (b >= 0xA0)
		b -= 0x80;

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
		case 0x50:
			GOTO(NONE, DCS_ENTRY);
		case 0x58:
		case 0x5E:
		case 0x5F:
			GOTO(NONE, SOS_PM_APC_STRING);
		case 0x5B:
			GOTO(NONE, CSI_ENTRY);
		case 0x5D:
			GOTO(NONE, OSC_STRING);
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

	case VTPARSE_CSI_ENTRY:
		if (is_c0(b))
			STAY(EXECUTE);
		if (b == 0x7F)
			STAY(IGNORE);
		if (b <= 0x2F)
			GOTO(COLLECT, CSI_INTERMEDIATE);
		if (b == 0x3A)
			GOTO(NONE, CSI_IGNORE);
		if (b <= 0x39 || b == 0x3B)
			GOTO(PARAM, CSI_PARAM);
		if (b <= 0x3F)
			GOTO(COLLECT, CSI_PARAM);
		GOTO(CSI_DISPATCH, GROUND);

	case VTPARSE_CSI_PARAM:
		if (is_c0(b))
			STAY(EXECUTE);
		if (b == 0x7F)
			STAY(IGNORE);
		if (b <= 0x2F)
			GOTO(COLLECT, CSI_INTERMEDIATE);
		if ((b >= 0x30 && b <= 0x39) || b == 0x3B)
			STAY(PARAM);
		if (b <= 0x3F)
			GOTO(NONE, CSI_IGNORE);	/* 3A and 3C-3F */
		GOTO(CSI_DISPATCH, GROUND);

	case VTPARSE_CSI_INTERMEDIATE:
		if (is_c0(b))
			STAY(EXECUTE);
		if (b <= 0x2F)
			STAY(COLLECT);
		if (b == 0x7F)
			STAY(IGNORE);
		if (b <= 0x3F)
			GOTO(NONE, CSI_IGNORE);
		GOTO(CSI_DISPATCH, GROUND);

	case VTPARSE_CSI_IGNORE:
		if (is_c0(b))
			STAY(EXECUTE);
		if (b <= 0x3F || b == 0x7F)
			STAY(IGNORE);
		GOTO(NONE, GROUND);	/* 40-7E: eaten, no dispatch */

	case VTPARSE_DCS_ENTRY:
		if (is_c0(b) || b == 0x7F)
			STAY(IGNORE);
		if (b <= 0x2F)
			GOTO(COLLECT, DCS_INTERMEDIATE);
		if (b == 0x3A)
			GOTO(NONE, DCS_IGNORE);
		if (b <= 0x39 || b == 0x3B)
			GOTO(PARAM, DCS_PARAM);
		if (b <= 0x3F)
			GOTO(COLLECT, DCS_PARAM);
		GOTO(NONE, DCS_PASSTHROUGH);	/* HOOK is passthrough's entry */

	case VTPARSE_DCS_PARAM:
		if (is_c0(b) || b == 0x7F)
			STAY(IGNORE);
		if (b <= 0x2F)
			GOTO(COLLECT, DCS_INTERMEDIATE);
		if ((b >= 0x30 && b <= 0x39) || b == 0x3B)
			STAY(PARAM);
		if (b <= 0x3F)
			GOTO(NONE, DCS_IGNORE);
		GOTO(NONE, DCS_PASSTHROUGH);

	case VTPARSE_DCS_INTERMEDIATE:
		if (is_c0(b) || b == 0x7F)
			STAY(IGNORE);
		if (b <= 0x2F)
			STAY(COLLECT);
		if (b <= 0x3F)
			GOTO(NONE, DCS_IGNORE);
		GOTO(NONE, DCS_PASSTHROUGH);

	case VTPARSE_DCS_PASSTHROUGH:
		if (b == 0x7F)
			STAY(IGNORE);
		STAY(PUT);	/* C0 controls included: the payload is binary */

	case VTPARSE_DCS_IGNORE:
		STAY(IGNORE);

	case VTPARSE_OSC_STRING:
		if (is_c0(b))
			STAY(IGNORE);
		STAY(OSC_PUT);

	case VTPARSE_SOS_PM_APC_STRING:
		STAY(IGNORE);

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

static void push_param(vtparse *p)
{
	if (p->nparams < VTPARSE_MAX_PARAMS)
		p->params[p->nparams++] = p->cur;
	/* else: "If more than 16 parameters arrive, all the extra parameters
	 * are silently ignored."  We count them anyway so a consumer can tell
	 * that truncation happened instead of silently mis-rendering. */
	p->nparams_seen++;
	p->cur = -1;
}

static void finish_params(vtparse *p)
{
	/* A trailing parameter has no separator to close it, and "CSI H" with
	 * no parameters at all must stay at zero. */
	if (p->cur >= 0 || p->nparams_seen > 0)
		push_param(p);
}

static void str_append(vtparse *p, unsigned char b)
{
	if (p->strlen < VTPARSE_STRING_MAX) {
		p->str[p->strlen++] = (char)b;
		p->str[p->strlen] = '\0';
	} else {
		p->str_truncated = 1;
	}
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
		if (b == ';') {
			push_param(p);
		} else {
			int32_t v = (p->cur < 0 ? 0 : p->cur) * 10 + (b - '0');
			/* "any parameter greater than 9999 is set to 9999" --
			 * clamp rather than wrap, so a hostile CSI 99999999m
			 * cannot overflow into a negative row/column. */
			if (v > VTPARSE_PARAM_MAX) {
				v = VTPARSE_PARAM_MAX;
				p->param_overflow = 1;
			}
			p->cur = v;
		}
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
	case VTPARSE_ACTION_CSI_DISPATCH:
		finish_params(p);
		emit(p, a, b);
		break;
	case VTPARSE_ACTION_HOOK:
		finish_params(p);
		emit(p, a, b);
		break;
	case VTPARSE_ACTION_PUT:
	case VTPARSE_ACTION_OSC_PUT:
		str_append(p, b);
		emit(p, a, b);
		break;
	case VTPARSE_ACTION_OSC_START:
		/* The spec defines osc_start as "initialization of the buffer":
		 * it is the buffer reset, not just a notification.  It has to
		 * happen here because the 8-bit OSC introducer (0x9D) reaches
		 * this state without passing through ESCAPE's clear. */
		p->str[0] = '\0';
		p->strlen = 0;
		p->str_truncated = 0;
		emit(p, a, 0);
		break;
	case VTPARSE_ACTION_UNHOOK:
	case VTPARSE_ACTION_OSC_END:
		emit(p, a, 0);
		break;
	default:
		break;
	}
}

/* Entry and exit actions, which the published table puts on the state box
 * rather than in a cell. */
static void state_exit(vtparse *p, vtparse_state s, unsigned char b)
{
	switch (s) {
	case VTPARSE_DCS_PASSTHROUGH:
		do_action(p, VTPARSE_ACTION_UNHOOK, b);
		break;
	case VTPARSE_OSC_STRING:
		do_action(p, VTPARSE_ACTION_OSC_END, b);
		break;
	default:
		break;
	}
}

static void state_entry(vtparse *p, vtparse_state s, unsigned char b)
{
	switch (s) {
	case VTPARSE_ESCAPE:
	case VTPARSE_CSI_ENTRY:
	case VTPARSE_DCS_ENTRY:
		do_action(p, VTPARSE_ACTION_CLEAR, b);
		break;
	case VTPARSE_DCS_PASSTHROUGH:
		/* The payload buffer belongs to the string, not to the params,
		 * and DCS_ENTRY's clear already reset it. */
		do_action(p, VTPARSE_ACTION_HOOK, b);
		break;
	case VTPARSE_OSC_STRING:
		do_action(p, VTPARSE_ACTION_OSC_START, b);
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
	/* Spec order: exit action of the old state, then the transition's own
	 * action, then the entry action of the new state. */
	state_exit(p, p->state, b);
	do_action(p, a, b);
	p->state = next;
	state_entry(p, next, b);
}

void vtparse_init(vtparse *p, vtparse_cb cb, void *user, int utf8)
{
	memset(p, 0, sizeof(*p));
	p->cb = cb;
	p->user = user;
	p->utf8 = utf8;
	p->state = VTPARSE_GROUND;
	p->cur = -1;
	vtutf8_init(&p->u8);
}

void vtparse_push_byte(vtparse *p, unsigned char b)
{
	/* UTF-8 mode only diverts the ground state.  Inside an escape sequence
	 * every byte is a 7-bit control character by definition, so there is
	 * nothing to decode -- and bytes >= 0x80 there are C1 controls, which
	 * the anywhere transitions must still see. */
	if (p->utf8 && p->state == VTPARSE_GROUND &&
	    (b >= 0x80 || p->u8.need)) {
		uint32_t cp;
		for (;;) {
			int r = vtutf8_push(&p->u8, b, &cp);
			if (r == VTUTF8_MORE)
				return;
			p->codepoint = cp;
			emit(p, VTPARSE_ACTION_PRINT, cp);
			if (r == VTUTF8_CP)
				return;
			/* VTUTF8_REPLACE: the byte was not consumed.  If it is
			 * a control or ESC it has to go to the state machine. */
			if (b < 0x80)
				break;
		}
	}
	step(p, b);
}

void vtparse_push(vtparse *p, const unsigned char *buf, size_t len)
{
	size_t i;
	for (i = 0; i < len; i++)
		vtparse_push_byte(p, buf[i]);
}

void vtparse_flush(vtparse *p)
{
	uint32_t cp;
	if (p->utf8 && vtutf8_flush(&p->u8, &cp)) {
		p->codepoint = cp;
		emit(p, VTPARSE_ACTION_PRINT, cp);
	}
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

/* ----------------------------------------------------------------- UTF-8 */

void vtutf8_init(vtutf8 *d)
{
	d->acc = 0;
	d->need = 0;
	d->lo = 0x80;
	d->hi = 0xBF;
}

/* Our invalid-sequence policy is the Unicode 15.0 recommendation ("Best
 * Practices for Using U+FFFD", section 3.9): emit exactly one U+FFFD per
 * *maximal subpart* of an ill-formed subsequence.  That means the ranges for
 * the second byte are checked per lead byte (E0 wants A0-BF, ED wants 80-9F,
 * F0 wants 90-BF, F4 wants 80-8F), which rejects overlongs, surrogates and
 * values above U+10FFFF at the earliest possible byte.  CPython's
 * bytes.decode('utf-8', errors='replace') implements the same rule, which is
 * what makes t/utf8_diff.py a meaningful differential test. */
int vtutf8_push(vtutf8 *d, unsigned char b, uint32_t *cp)
{
	if (d->need == 0) {
		if (b < 0x80) {
			*cp = b;
			return VTUTF8_CP;
		}
		if (b >= 0xC2 && b <= 0xDF) {
			d->acc = b & 0x1Fu;
			d->need = 1;
			d->lo = 0x80;
			d->hi = 0xBF;
		} else if (b >= 0xE0 && b <= 0xEF) {
			d->acc = b & 0x0Fu;
			d->need = 2;
			d->lo = (b == 0xE0) ? 0xA0 : 0x80;
			d->hi = (b == 0xED) ? 0x9F : 0xBF;
		} else if (b >= 0xF0 && b <= 0xF4) {
			d->acc = b & 0x07u;
			d->need = 3;
			d->lo = (b == 0xF0) ? 0x90 : 0x80;
			d->hi = (b == 0xF4) ? 0x8F : 0xBF;
		} else {
			/* C0, C1 (overlong two-byte leads), F5-FF, and bare
			 * continuation bytes 80-BF: each is its own maximal
			 * subpart of length one. */
			*cp = 0xFFFD;
			return VTUTF8_CP;
		}
		return VTUTF8_MORE;
	}
	if (b >= d->lo && b <= d->hi) {
		d->acc = (d->acc << 6) | (b & 0x3Fu);
		d->lo = 0x80;
		d->hi = 0xBF;
		if (--d->need == 0) {
			*cp = d->acc;
			return VTUTF8_CP;
		}
		return VTUTF8_MORE;
	}
	/* The maximal subpart ended before this byte.  Report it and make the
	 * caller re-push b, which may legally start a new sequence. */
	vtutf8_init(d);
	*cp = 0xFFFD;
	return VTUTF8_REPLACE;
}

int vtutf8_flush(vtutf8 *d, uint32_t *cp)
{
	if (d->need == 0)
		return 0;
	vtutf8_init(d);
	*cp = 0xFFFD;
	return 1;
}

/* ------------------------------------------------------------------ width */

#include "width_table.h"

static int in_ranges(const struct vtw_range *r, size_t n, uint32_t cp)
{
	size_t lo = 0, hi = n;
	while (lo < hi) {
		size_t mid = lo + (hi - lo) / 2;
		if (cp < r[mid].lo)
			hi = mid;
		else if (cp > r[mid].hi)
			lo = mid + 1;
		else
			return 1;
	}
	return 0;
}

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))

int vtwidth(uint32_t cp)
{
	if (cp > 0x10FFFF)
		return -1;
	if (in_ranges(vtw_nonprint, NELEM(vtw_nonprint), cp))
		return -1;
	if (in_ranges(vtw_zero, NELEM(vtw_zero), cp))
		return 0;
	if (in_ranges(vtw_wide, NELEM(vtw_wide), cp))
		return 2;
	return 1;
}

const char *vtwidth_unicode_version(void)
{
	return VTW_UNICODE_VERSION;
}
