/* corpus_test.c -- ORACLE 4: real escape sequences, exact event streams.
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Every case is a byte string a real program actually emits, paired with the
 * exact event stream it must produce.  The rendering is deliberately terse so
 * the expectation fits on one line and can be eyeballed:
 *
 *   p:XXXX        PRINT, codepoint in hex
 *   x:XX          EXECUTE, byte in hex
 *   E:<i><f>      ESC_DISPATCH, intermediates then final
 *   C:<i><f>[p;p] CSI_DISPATCH, intermediates, final, parameters
 *                 (-1 is an omitted parameter; "+n" means n were dropped)
 *   H:<i><f>[p;p] HOOK (start of a DCS)
 *   U/n=<s>       UNHOOK after n PUT events, with the payload
 *   O{            OSC_START
 *   }O/n=<s>      OSC_END after n OSC_PUT events, with the payload
 *   @<STATE>      the parser's state at end of input
 *
 * The PUT/OSC_PUT counts are in the expectation on purpose: they prove the
 * per-byte string callbacks fire, which a payload-only check would not.
 */
#include "../vtparse.h"
#include "tap.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static char out[4096];
static size_t outlen;
static int nput;

static void add(const char *fmt, ...)
{
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vsnprintf(out + outlen, sizeof(out) - outlen, fmt, ap);
	va_end(ap);
	if (n > 0)
		outlen += (size_t)n;
}

static void add_seq(vtparse *p, char kind, uint32_t ch)
{
	int i;
	add(" %c:", kind);
	for (i = 0; i < p->nintermediates; i++)
		add("%c", p->intermediates[i]);
	add("%c[", (char)ch);
	for (i = 0; i < p->nparams; i++)
		add("%s%d", i ? ";" : "", p->params[i]);
	if (p->nparams_seen > p->nparams)
		add("+%d", p->nparams_seen - p->nparams);
	add("]");
}

static void on_event(vtparse *p, vtparse_action a, uint32_t ch)
{
	switch (a) {
	case VTPARSE_ACTION_PRINT:
		add(" p:%04X", ch);
		break;
	case VTPARSE_ACTION_EXECUTE:
		add(" x:%02X", ch);
		break;
	case VTPARSE_ACTION_ESC_DISPATCH: {
		int i;
		add(" E:");
		for (i = 0; i < p->nintermediates; i++)
			add("%c", p->intermediates[i]);
		add("%c", (char)ch);
		break;
	}
	case VTPARSE_ACTION_CSI_DISPATCH:
		add_seq(p, 'C', ch);
		break;
	case VTPARSE_ACTION_HOOK:
		nput = 0;
		add_seq(p, 'H', ch);
		break;
	case VTPARSE_ACTION_PUT:
	case VTPARSE_ACTION_OSC_PUT:
		nput++;
		break;
	case VTPARSE_ACTION_UNHOOK:
		add(" U/%d=%s", nput, p->str);
		break;
	case VTPARSE_ACTION_OSC_START:
		nput = 0;
		add(" O{");
		break;
	case VTPARSE_ACTION_OSC_END:
		add(" }O/%d=%s", nput, p->str);
		break;
	default:
		break;
	}
}

struct tcase {
	const char *name;
	const char *in;		/* NUL-free, so a C string is enough */
	int utf8;
	const char *want;
};

static const struct tcase cases[] = {
/* --- SGR colour ---------------------------------------------------------- */
{"SGR bold red", "\033[1;31m", 0, "C:m[1;31] @GROUND"},
{"SGR reset, no params", "\033[m", 0, "C:m[] @GROUND"},
{"SGR explicit 0 differs from absent", "\033[0m", 0, "C:m[0] @GROUND"},
{"SGR 256-colour", "\033[38;5;208m", 0, "C:m[38;5;208] @GROUND"},
{"SGR truecolour (semicolon form)", "\033[38;2;10;20;30m", 0,
	"C:m[38;2;10;20;30] @GROUND"},
/* The DEC table sends any CSI containing 3A to CSI_IGNORE, so the ECMA-48
 * colon form of SGR -- which modern terminals do accept -- is swallowed whole.
 * This is the single biggest divergence from real terminals; see README. */
{"SGR colon form is dropped (CSI_IGNORE)", "\033[38:2::10:20:30m", 0,
	"@GROUND"},
/* --- cursor movement ----------------------------------------------------- */
{"CUP absolute", "\033[10;20H", 0, "C:H[10;20] @GROUND"},
{"CUU default param", "\033[A", 0, "C:A[] @GROUND"},
{"CUP with omitted first param", "\033[;5H", 0, "C:H[-1;5] @GROUND"},
{"ED clear screen then home", "\033[2J\033[H", 0, "C:J[2] C:H[] @GROUND"},
{"save/restore cursor (ESC 7 / ESC 8)", "\0337\0338", 0, "E:7 E:8 @GROUND"},
/* --- OSC ----------------------------------------------------------------- */
{"OSC 0 set title, ST terminated", "\033]0;hi there\033\\", 0,
	"O{ }O/10=0;hi there E:\\ @GROUND"},
/* BEL is a C0 control: the published table IGNOREs it inside OSC_STRING, so
 * xterm's traditional BEL-terminated OSC never closes.  Asserted here as
 * documentation of a known divergence, not as desirable behaviour. */
{"OSC with BEL terminator is NOT closed", "\033]0;hi\007", 0,
	"O{ @OSC_STRING"},
{"OSC 8 hyperlink, 8-bit ST", "\033]8;;http://x/\234", 0,
	"O{ }O/12=8;;http://x/ @GROUND"},
/* --- private modes ------------------------------------------------------- */
{"DECSET bracketed paste", "\033[?2004h", 0, "C:?h[2004] @GROUND"},
{"DECRST bracketed paste", "\033[?2004l", 0, "C:?l[2004] @GROUND"},
{"DECSET alt screen", "\033[?1049h", 0, "C:?h[1049] @GROUND"},
{"DECRST cursor visibility", "\033[?25l", 0, "C:?l[25] @GROUND"},
{"bracketed paste markers around text",
	"\033[200~hi\033[201~", 0, "C:~[200] p:0068 p:0069 C:~[201] @GROUND"},
{"DECSCUSR with intermediate", "\033[2 q", 0, "C: q[2] @GROUND"},
{"DECSTBM then DECSTR", "\033[1;24r\033[!p", 0,
	"C:r[1;24] C:!p[] @GROUND"},
/* --- DCS ----------------------------------------------------------------- */
{"DECRQSS (DCS with intermediate)", "\033P1;2+q7470\033\\", 0,
	"H:+q[1;2] U/4=7470 E:\\ @GROUND"},
{"Sixel header, payload passed through", "\033Pq#0;2;0;0;0\033\\", 0,
	"H:q[] U/10=#0;2;0;0;0 E:\\ @GROUND"},
/* --- SOS/PM/APC ---------------------------------------------------------- */
/* A Kitty graphics command.  The published machine has no payload callbacks for
 * APC at all, so the entire command is discarded; only the ST's ESC \ shows. */
{"Kitty graphics APC is discarded",
	"\033_Ga=T,f=100,s=1,v=1;iVBORw0KGgo=\033\\", 0, "E:\\ @GROUND"},
/* --- 8-bit controls ------------------------------------------------------ */
{"8-bit CSI (0x9B)", "\233""31m", 0, "C:m[31] @GROUND"},
{"8-bit OSC (0x9D) and ST (0x9C)", "\235""2;t\234", 0,
	"O{ }O/3=2;t @GROUND"},
{"C1 IND executes", "\204", 0, "x:84 @GROUND"},
/* --- C0 handling -------------------------------------------------------- */
{"CR LF in ground", "a\r\nb", 0, "p:0061 x:0D x:0A p:0062 @GROUND"},
/* "C0 controls are executed immediately during the recognition of a control
 * sequence" -- the CR lands between the parameters and does not break them. */
{"C0 executes mid-sequence without breaking params", "\033[1\r;2m", 0,
	"x:0D C:m[1;2] @GROUND"},
{"BEL mid-sequence", "\033[1\0077m", 0, "x:07 C:m[17] @GROUND"},
{"CAN aborts a sequence", "\033[1;2\030m", 0, "x:18 p:006D @GROUND"},
{"SUB aborts a sequence", "\033[1;2\032m", 0, "x:1A p:006D @GROUND"},
{"ESC aborts and restarts", "\033[1;2\033[3m", 0, "C:m[3] @GROUND"},
/* --- truncation --------------------------------------------------------- */
/* A truncated sequence must leave the parser mid-sequence: the bytes may be
 * split across read() calls, and throwing the partial sequence away would
 * corrupt the next one. */
{"truncated CSI stays pending", "\033[12;", 0, "@CSI_PARAM"},
{"truncated CSI resumes correctly", "\033[12;34m", 0, "C:m[12;34] @GROUND"},
{"truncated DCS stays in passthrough", "\033Pq#1", 0, "H:q[] @DCS_PASSTHROUGH"},
/* --- parameter limits --------------------------------------------------- */
/* Spec: at most 16 parameters are stored, extras silently ignored. */
{"20 parameters: 16 kept, 4 dropped",
	"\033[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16;17;18;19;20m", 0,
	"C:m[1;2;3;4;5;6;7;8;9;10;11;12;13;14;15;16+4] @GROUND"},
/* Spec: "any parameter greater than 9999 is set to 9999". */
{"parameter value clamped to 9999", "\033[99999999H", 0,
	"C:H[9999] @GROUND"},
{"third intermediate overflows the two-slot buffer", "\033[!\"#p", 0,
	"C:!\"p[] @GROUND"},
/* --- DEL and GR --------------------------------------------------------- */
{"DEL prints in ground (table says 20-7F print)", "\177", 0,
	"p:007F @GROUND"},
{"DEL is ignored inside CSI", "\033[1\1772m", 0, "C:m[12] @GROUND"},
/* The GR byte is ROUTED like its GL twin (both print) but the byte handed to
 * the callback is the original: in a Latin-1 G1 set 0xE1 is a-acute, so the
 * consumer, not the parser, decides what glyph it means. */
{"GR byte routes like its GL twin but keeps its value", "\341", 0,
	"p:00E1 @GROUND"},
/* --- charset designation ------------------------------------------------ */
{"designate G0 ASCII", "\033(B", 0, "E:(B @GROUND"},
{"designate G0 DEC graphics", "\033(0", 0, "E:(0 @GROUND"},
/* --- UTF-8 mode --------------------------------------------------------- */
{"UTF-8 2-byte", "h\303\251llo", 1,
	"p:0068 p:00E9 p:006C p:006C p:006F @GROUND"},
{"UTF-8 CJK (wide)", "\346\227\245\346\234\254", 1, "p:65E5 p:672C @GROUND"},
{"UTF-8 emoji outside the BMP", "\360\237\221\215", 1, "p:1F44D @GROUND"},
{"UTF-8 combining mark", "e\314\201", 1, "p:0065 p:0301 @GROUND"},
{"UTF-8 invalid byte becomes U+FFFD", "a\377b", 1,
	"p:0061 p:FFFD p:0062 @GROUND"},
{"UTF-8 escape sequence still parses", "\033[31m\303\251", 1,
	"C:m[31] p:00E9 @GROUND"},
/* An ESC arriving mid-character must flush the partial character and then be
 * honoured as an escape introducer, or a split write would eat the sequence. */
{"UTF-8 truncated char followed by ESC", "\303\033[0m", 1,
	"p:FFFD C:m[0] @GROUND"},
/* Without UTF-8 mode the two bytes of U+00E9 are just two GR graphics. */
{"8-bit mode treats UTF-8 bytes as two GR graphics", "\303\251", 0,
	"p:00C3 p:00A9 @GROUND"},
{NULL, NULL, 0, NULL}
};

/* Widths are a separate oracle (t/width_diff.c); these few are the spot checks
 * that the table is wired up at all. */
static void width_spots(void)
{
	OK(vtwidth('a') == 1, "width 'a' == 1");
	OK(vtwidth(0x0301) == 0, "width U+0301 combining acute == 0");
	OK(vtwidth(0x65E5) == 2, "width U+65E5 CJK == 2");
	OK(vtwidth(0xFF01) == 2, "width U+FF01 fullwidth == 2");
	OK(vtwidth(0x1F44D) == 2, "width U+1F44D emoji == 2");
	OK(vtwidth(0x1B) == -1, "width U+001B ESC == -1");
	OK(vtwidth(0x200B) == 0, "width U+200B zero width space == 0");
	OK(vtwidth(0x1160) == 0, "width U+1160 hangul jamo medial == 0");
	OK(vtwidth(0x110000) == -1, "width above U+10FFFF == -1");
}

/* bytewise: feed through vtparse_push_byte() instead of vtparse_push(), which
 * is how a consumer with a one-byte-at-a-time source (a pty read of 1) would
 * use the library.  The two must be indistinguishable. */
static void render(const struct tcase *c, int bytewise)
{
	vtparse p;
	size_t i, n = strlen(c->in);

	outlen = 0;
	out[0] = '\0';
	nput = 0;
	vtparse_init(&p, on_event, NULL, c->utf8);
	if (bytewise) {
		for (i = 0; i < n; i++)
			vtparse_push_byte(&p, (unsigned char)c->in[i]);
	} else {
		vtparse_push(&p, (const unsigned char *)c->in, n);
	}
	vtparse_flush(&p);
	add(" @%s", vtparse_state_name(p.state));
}

int main(void)
{
	const struct tcase *c;
	int bytewise_ok = 0, ncases = 0;

	for (c = cases; c->name; c++) {
		char whole[sizeof(out)];
		render(c, 0);
		/* out starts with a space; skip it. */
		OK(strcmp(out + 1, c->want) == 0, "%s", c->name);
		if (strcmp(out + 1, c->want) != 0)
			printf("#   want: %s\n#   got:  %s\n", c->want,
			       out + 1);
		memcpy(whole, out, outlen + 1);
		render(c, 1);
		ncases++;
		if (strcmp(whole, out) == 0)
			bytewise_ok++;
		else
			printf("# bytewise differs for %s: %s vs %s\n",
			       c->name, whole + 1, out + 1);
	}
	OK(bytewise_ok == ncases, "vtparse_push_byte matches vtparse_push on"
	   " all %d cases", ncases);
	width_spots();

	/* Hard invariant: the struct is self-contained, so a parser can live on
	 * the stack and the library can be used with no allocator at all. */
	OK(sizeof(vtparse) < 2048, "vtparse struct is %zu bytes, no heap needed",
	   sizeof(vtparse));
	return tap_fail;
}
