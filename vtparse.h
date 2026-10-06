/* vtparse.h -- VT500-series escape sequence parser, UTF-8 decoder, wcwidth.
 *
 * Copyright (C) 2026  PLACEHOLDER
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This is a byte-at-a-time push parser.  You hand it one byte at a time (or a
 * buffer, which it walks one byte at a time) and it calls you back with the
 * decoded events.  It is a faithful encoding of the DEC/ECMA-48 state machine
 * published at <https://vt100.net/emu/dec_ansi_parser>.
 *
 * WHY a header: this is a library with two in-tree consumers already (the CLI
 * and the test harness), so the declarations have to live somewhere shared.
 *
 * WHY the parser never allocates: a terminal emulator parses attacker-supplied
 * bytes on its hot path -- every byte of every program's stdout.  An allocation
 * there is a failure mode (OOM mid-sequence) and a DoS vector (a 2GB OSC string
 * from a hostile `cat`).  Every buffer here is fixed-size and inline in the
 * struct, so vtparse_push() cannot fail, cannot block, and cannot grow.  The
 * cost is that long OSC/DCS payloads are truncated; see VTPARSE_STRING_MAX.
 */
#ifndef VTPARSE_H
#define VTPARSE_H

#include <stddef.h>
#include <stdint.h>

/* The 14 states of the published table.  The names and the order are the
 * spec's; the test harness indexes tables by these values. */
typedef enum {
	VTPARSE_GROUND = 0,
	VTPARSE_ESCAPE,
	VTPARSE_ESCAPE_INTERMEDIATE,
	VTPARSE_CSI_ENTRY,
	VTPARSE_CSI_PARAM,
	VTPARSE_CSI_INTERMEDIATE,
	VTPARSE_CSI_IGNORE,
	VTPARSE_DCS_ENTRY,
	VTPARSE_DCS_PARAM,
	VTPARSE_DCS_INTERMEDIATE,
	VTPARSE_DCS_PASSTHROUGH,
	VTPARSE_DCS_IGNORE,
	VTPARSE_OSC_STRING,
	VTPARSE_SOS_PM_APC_STRING,
	VTPARSE_NSTATES
} vtparse_state;

/* The spec's actions.  NONE is the spec's blank cell (a transition with no
 * action); IGNORE is the spec's explicit "ignore".  They behave identically at
 * runtime but are kept distinct so the transition-table test can assert an
 * exact match against the published table rather than a lossy one.
 *
 * CLEAR, COLLECT and PARAM are internal bookkeeping and are never reported to
 * the callback.  Everything from PRINT down is reported. */
typedef enum {
	VTPARSE_ACTION_NONE = 0,
	VTPARSE_ACTION_IGNORE,
	VTPARSE_ACTION_CLEAR,
	VTPARSE_ACTION_COLLECT,
	VTPARSE_ACTION_PARAM,
	VTPARSE_ACTION_PRINT,
	VTPARSE_ACTION_EXECUTE,
	VTPARSE_ACTION_ESC_DISPATCH,
	VTPARSE_ACTION_CSI_DISPATCH,
	VTPARSE_ACTION_HOOK,
	VTPARSE_ACTION_PUT,
	VTPARSE_ACTION_UNHOOK,
	VTPARSE_ACTION_OSC_START,
	VTPARSE_ACTION_OSC_PUT,
	VTPARSE_ACTION_OSC_END,
	VTPARSE_NACTIONS
} vtparse_action;

/* The spec: "a maximum of 16 parameters need be stored.  If more than 16
 * parameters arrive, all the extra parameters are silently ignored."  And:
 * "any parameter greater than 9999 (decimal) is set to 9999 (decimal)."
 * Both clamps are implemented exactly as written -- they are the reason the
 * parser needs no dynamic storage.  Note that real xterm clamps values at
 * 65535 and keeps 30 parameters; we follow the published spec because the
 * published spec is our oracle. */
#define VTPARSE_MAX_PARAMS      16
#define VTPARSE_PARAM_MAX       9999
#define VTPARSE_MAX_INTERMEDIATES 2	/* spec: two is enough for every real seq */
#define VTPARSE_STRING_MAX      512	/* OSC/DCS payload kept, in bytes */

typedef struct vtparse vtparse;
typedef void (*vtparse_cb)(vtparse *p, vtparse_action action, uint32_t ch);

struct vtparse {
	vtparse_state state;
	vtparse_cb cb;
	void *user;		/* opaque, for the callback */

	/* Current sequence, valid during *_DISPATCH / HOOK callbacks. */
	int32_t params[VTPARSE_MAX_PARAMS];
	int nparams;		/* number stored, <= VTPARSE_MAX_PARAMS */
	int32_t cur;		/* param being accumulated; -1 = no digits yet */
	int nparams_seen;	/* number that arrived, may exceed the above */
	int param_overflow;	/* a value was clamped to VTPARSE_PARAM_MAX */
	unsigned char intermediates[VTPARSE_MAX_INTERMEDIATES];
	int nintermediates;
	int intermediate_overflow;

	/* OSC/DCS payload, NUL-terminated, truncated at VTPARSE_STRING_MAX. */
	char str[VTPARSE_STRING_MAX + 1];
	size_t strlen;
	int str_truncated;

	/* The codepoint for a PRINT action.  Always the byte itself until
	 * UTF-8 decoding lands on top of this field. */
	uint32_t codepoint;

};

void vtparse_init(vtparse *p, vtparse_cb cb, void *user);

/* Feed bytes.  Never allocates, never reads past buf[len-1], never fails. */
void vtparse_push(vtparse *p, const unsigned char *buf, size_t len);
void vtparse_push_byte(vtparse *p, unsigned char b);

const char *vtparse_state_name(vtparse_state s);
const char *vtparse_action_name(vtparse_action a);

/* The published transition table, exposed for the table-driven test.  This is
 * the single source of truth the engine itself uses -- the test compares it
 * against an independent transcription of the spec, and separately drives the
 * live parser to prove the engine obeys it. */
/* *transition is 1 when the table cell names a target state (so the exit
 * action of the old state and the entry action of the new one must run) and 0
 * when the cell leaves the state alone.  The distinction matters because the
 * "anywhere" cells name their target even when it is the state we are already
 * in: ESC inside an escape sequence must re-run CLEAR. */
void vtparse_lookup(vtparse_state s, unsigned char b, vtparse_action *action,
		    vtparse_state *next, int *transition);

#endif /* VTPARSE_H */
