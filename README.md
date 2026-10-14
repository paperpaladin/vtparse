# vtparse

A byte-at-a-time VT500-series escape sequence parser in C11, implemented as the
exact DEC/ECMA-48 state machine, with built-in UTF-8 decoding and `wcwidth`-style
display widths and no dependencies beyond libc.

## Build and test

```
$ make test
CC='cc' CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -O2 -g -ffile-prefix-map=$(CURDIR)=.' ./t/run.sh
=== building (CC=cc CFLAGS=-std=c11 -Wall -Wextra -Wpedantic -O2 -g)

--- oracle 1: published state table (14 states x 256 bytes)
ok - prefix 0 reaches GROUND (got GROUND)
ok - prefix 1 reaches ESCAPE (got ESCAPE)
ok - prefix 2 reaches ESCAPE_INTERMEDIATE (got ESCAPE_INTERMEDIATE)
ok - prefix 3 reaches CSI_ENTRY (got CSI_ENTRY)
ok - prefix 4 reaches CSI_PARAM (got CSI_PARAM)
ok - prefix 5 reaches CSI_INTERMEDIATE (got CSI_INTERMEDIATE)
ok - prefix 6 reaches CSI_IGNORE (got CSI_IGNORE)
ok - prefix 7 reaches DCS_ENTRY (got DCS_ENTRY)
ok - prefix 8 reaches DCS_PARAM (got DCS_PARAM)
ok - prefix 9 reaches DCS_INTERMEDIATE (got DCS_INTERMEDIATE)
ok - prefix 10 reaches DCS_PASSTHROUGH (got DCS_PASSTHROUGH)
ok - prefix 11 reaches DCS_IGNORE (got DCS_IGNORE)
ok - prefix 12 reaches OSC_STRING (got OSC_STRING)
ok - prefix 13 reaches SOS_PM_APC_STRING (got SOS_PM_APC_STRING)
ok - 3584 (state,byte) pairs enumerated (14 states x 256)
ok - published table is total: 3584/3584 cells defined
ok - vtparse_lookup matches the published table: 3584/3584 pairs
ok - live parser state transitions match: 3584/3584 pairs
ok - live parser callbacks match: 3584/3584 pairs

--- oracle 4: real escape sequence corpus
ok - SGR bold red
ok - SGR reset, no params
ok - SGR explicit 0 differs from absent
ok - SGR 256-colour
ok - SGR truecolour (semicolon form)
ok - SGR colon form is dropped (CSI_IGNORE)
ok - CUP absolute
ok - CUU default param
ok - CUP with omitted first param
ok - ED clear screen then home
ok - save/restore cursor (ESC 7 / ESC 8)
ok - OSC 0 set title, ST terminated
ok - OSC with BEL terminator is NOT closed
ok - OSC 8 hyperlink, 8-bit ST
ok - DECSET bracketed paste
ok - DECRST bracketed paste
ok - DECSET alt screen
ok - DECRST cursor visibility
ok - bracketed paste markers around text
ok - DECSCUSR with intermediate
ok - DECSTBM then DECSTR
ok - DECRQSS (DCS with intermediate)
ok - Sixel header, payload passed through
ok - Kitty graphics APC is discarded
ok - 8-bit CSI (0x9B)
ok - 8-bit OSC (0x9D) and ST (0x9C)
ok - C1 IND executes
ok - CR LF in ground
ok - C0 executes mid-sequence without breaking params
ok - BEL mid-sequence
ok - CAN aborts a sequence
ok - SUB aborts a sequence
ok - ESC aborts and restarts
ok - truncated CSI stays pending
ok - truncated CSI resumes correctly
ok - truncated DCS stays in passthrough
ok - 20 parameters: 16 kept, 4 dropped
ok - parameter value clamped to 9999
ok - third intermediate overflows the two-slot buffer
ok - DEL prints in ground (table says 20-7F print)
ok - DEL is ignored inside CSI
ok - GR byte routes like its GL twin but keeps its value
ok - designate G0 ASCII
ok - designate G0 DEC graphics
ok - UTF-8 2-byte
ok - UTF-8 CJK (wide)
ok - UTF-8 emoji outside the BMP
ok - UTF-8 combining mark
ok - UTF-8 invalid byte becomes U+FFFD
ok - UTF-8 escape sequence still parses
ok - UTF-8 truncated char followed by ESC
ok - 8-bit mode treats UTF-8 bytes as two GR graphics
ok - vtparse_push_byte matches vtparse_push on all 52 cases
ok - width 'a' == 1
ok - width U+0301 combining acute == 0
ok - width U+65E5 CJK == 2
ok - width U+FF01 fullwidth == 2
ok - width U+1F44D emoji == 2
ok - width U+001B ESC == -1
ok - width U+200B zero width space == 0
ok - width U+1160 hangul jamo medial == 0
ok - width above U+10FFFF == -1
ok - vtparse struct is 664 bytes, no heap needed

--- oracle 2: width vs libc wcwidth(3) over the BMP
# locale=en_US.UTF-8, vtparse table Unicode 15.1.0
# U+0897..U+0897  vtparse=-1 glibc=0
# U+1B4E..U+1B4F  vtparse=-1 glibc=1
# U+1B7F..U+1B7F  vtparse=-1 glibc=1
# U+1C89..U+1C8A  vtparse=-1 glibc=1
# U+2427..U+2429  vtparse=-1 glibc=1
# U+2630..U+2637  vtparse=1 glibc=2
# U+268A..U+268F  vtparse=1 glibc=2
# U+31E4..U+31E5  vtparse=-1 glibc=2
# U+3248..U+324F  vtparse=1 glibc=2
# U+4DC0..U+4DFF  vtparse=1 glibc=2
# U+A7CB..U+A7CD  vtparse=-1 glibc=1
# U+A7DA..U+A7DC  vtparse=-1 glibc=1
# 65433 agree, 103 differ in 12 ranges, 99.8428% agreement
ok - vtwidth agrees with wcwidth(3) on 65433/65536 BMP codepoints (99.8428%)
ok - 103 disagreements reported as 12 ranges

--- oracle 3: UTF-8 decode vs CPython
ok - every single byte: 256/256 match CPython
ok - two-byte: every lead x sampled second byte: 1664/1664 match CPython
ok - two-byte: every valid 2-byte sequence: 1920/1920 match CPython
ok - three-byte: every lead x sampled continuations: 2704/2704 match CPython
ok - three-byte: exhaustive E0-EF x 80-BF x 80-BF: 65536/65536 match CPython
ok - three-byte: boundary continuations: 128/128 match CPython
ok - four-byte: every lead x boundary continuations: 720/720 match CPython
ok - known hard cases: 26/26 match CPython
ok - fuzz: random bytes: 8000/8000 match CPython
ok - fuzz: random bytes biased to lead/continuation bytes: 8000/8000 match CPython
# 88954 cases, 88954 agree, 0 differ
ok - 88954/88954 sequences decode identically to CPython

--- CLI event stream
ok - CLI event stream matches t/cli.expected

--- random input smoke test
ok - 400kB of /dev/urandom through vtparse (8-bit)
ok - 400kB of /dev/urandom through vtparse -u

101 passed, 0 failed
```

Also `make test-asan` (the same 98 tests under `-fsanitize=address,undefined`) and
`make test-valgrind`.

## Usage

```c
#include "vtparse.h"

static void on_event(vtparse *p, vtparse_action a, uint32_t ch)
{
        if (a == VTPARSE_ACTION_CSI_DISPATCH && ch == 'm')
                apply_sgr(p->params, p->nparams);
        else if (a == VTPARSE_ACTION_PRINT)
                put_glyph(p->codepoint, vtwidth(p->codepoint));
}

vtparse p;
vtparse_init(&p, on_event, NULL, /* utf8 */ 1);
vtparse_push(&p, buf, n);          /* as many times as you like */
vtparse_flush(&p);                 /* end of input */
```

And from a shell:

```
$ printf '\033[1;31mhi\033]0;win\033\\\343\201\202' | ./vtparse -u
CSI_DISPATCH 'm' inter="" params=[1,31]
PRINT U+0068 'h' w=1
PRINT U+0069 'i' w=1
OSC_START
OSC_PUT 0x30
OSC_PUT 0x3B
OSC_PUT 0x77
OSC_PUT 0x69
OSC_PUT 0x6E
OSC_END payload="0;win"
ESC_DISPATCH '\' inter=""
PRINT U+3042 w=2
EOF state=GROUND
```

## Why this exists

Everything that reads terminal output has to do this: a multiplexer, a terminal
emulator, a CI log viewer, a `script` replayer, a test harness that asserts what
a TUI drew.  They almost all do it with a hand-rolled regex or an ad-hoc switch
that gets `CSI ? 2 0 0 4 h` right and then falls apart on a DCS, an 8-bit CSI, a
BEL in the middle of a parameter list, or a sequence split across two `read()`
calls.  The DEC state machine handles all of those by construction and has been
published, with a diagram, for twenty-odd years. It is less work to encode it
than to debug the regex.

Second reason: it is a parser for untrusted input on a hot path.  Every byte any
program writes to a terminal goes through it, including bytes written
deliberately to break it.  That makes "never allocates, never reads ahead" worth
more than any feature, and it makes a state machine with 14 states and a fixed
struct the right shape.

Third: codepoints are not columns.  A parser that hands back bytes and leaves
width to the caller has pushed the hardest part of the problem downstream, so the
width table is in here, and it is checked against libc, not asserted.

## The oracle

Four external references, in order of strength.

1. The published state table.  <https://vt100.net/emu/dec_ansi_parser> is a
complete specification: 14 states, the actions on each cell, the entry/exit
actions on each state, and the "anywhere" transitions.  `t/table_test.c` contains
a second, independent transcription of it, written as declarative range lists in
the spec's own notation (`{"00-17,19,1C-1F", A(EXECUTE), STAY}`), not as
the if/switch chain `vtparse.c` uses, and asserts:

- the spec table is total: 3584/3584 cells defined, no byte falls through;
- `vtparse_lookup()` matches it on 3584/3584 (state, byte) pairs, down to the
  action and to whether the cell is a transition or a stay;
- the live parser, driven into each of the 14 states with a real byte prefix
  and then fed the byte, lands in the predicted state on 3584/3584 pairs and
  fires the predicted callback on 3584/3584.

The brief for this project asked for 4096 pairs, assuming 16 states.  The
published table has 14: the DCS and string states are often described loosely
as "DCS_*" and "SOS/PM/APC", which can be miscounted as more.  3584 is the real
number and it is 100%.

2. CPython's UTF-8 decoder.  `t/utf8_diff.py` generates 88,954 byte
sequences, every single byte, every two-byte sequence with a sampled or
exhaustive second byte, the exhaustive 3-byte space `E0-EF x 80-BF x 80-BF`,
four-byte boundary cases, the Unicode TR#36 examples, overlongs, surrogates,
CESU-8, values above U+10FFFF, truncations, and 16,000 fuzz cases, and compares
our codepoints against `bytes.decode('utf-8', errors='replace')`.  Agreement is
88954/88954.

Our invalid-sequence policy, stated plainly: **one U+FFFD per maximal subpart**,
the Unicode "Best Practices for Using U+FFFD" rule.  `E0 80 80` is three U+FFFD
(0xE0 admits only A0-BF next, so the ill-formed subpart is one byte and each
trailing byte is its own), while `F0 9F 92` is one U+FFFD (a truncated but
so-far-well-formed prefix is a single subpart).  A truncated sequence at end of
input becomes U+FFFD on `vtparse_flush()`, not before: the next `read()` may
complete it.  This is the same rule CPython uses, which is what makes the
comparison meaningful, not circular.

3. libc `wcwidth(3)`.  `t/width_diff.c` compares `vtwidth()` with glibc over
the entire BMP in `en_US.UTF-8`.  Agreement is **65433/65536 = 99.8428%**.  The
103 disagreements are listed below, under Status, with the reason. They are not
a bug to be fixed but a version skew to be known about.

4. A corpus of sequences real programs emit.  `t/corpus_test.c`: 52 cases,
each asserting an exact event stream including the per-byte `PUT`/`OSC_PUT`
counts, and each replayed a second time one byte at a time to prove the chunking
does not matter.

Supporting, not oracles: `make test-asan` and `make test-valgrind` over 400kB of
`/dev/urandom` in both parser modes.  The no-allocation claim is the kind of
thing a sanitizer can check and a reviewer cannot.

## Status

Works, tested, and used by the CLI in this repo: the full 14-state machine
including DCS and the string states, 7-bit and 8-bit control introducers,
parameters with the spec's clamping, intermediates, OSC/DCS payload accumulation,
UTF-8 decoding with the maximal-subpart rule, and widths.

### The suite can tell a crash from a failure

`t/run.sh`'s `run()` helper used to discard the exit status on purpose:

```sh
if "$@" 2>&1 | tee -a "$OUT"; then :; else :; fi
```

A pipeline's status is the last command's, so that was `tee`'s, and even
capturing it would not have mattered, because the score is counted by
grepping the log for `ok - ` lines. A test binary that dies partway through
leaves its earlier `ok` lines in the file, emits no `not ok`, and the suite
prints "N passed, 0 failed" with a smaller N that nothing asserts. The
3584/3584 table check and the 52 corpus cases could both have become a
handful without the suite noticing.

Under `make test-asan` it was the difference between a sanitizer report and
a green run: ASan exits 1, and that was the number going nowhere.

Measured on the helper, against a command that prints one `ok` line and then
exits 134:

```
OLD run():  ok=1  not-ok=0
NEW run():  ok=1  not-ok=1
```

`tzf/t/run.sh` in the same directory already did it this way.

## What does not work

- `SGR` with colon sub-parameters is silently dropped.  The published table
  routes any CSI containing `3A` to CSI_IGNORE, so `CSI 38:2::10:20:30 m`: the
  ECMA-48 colon form of 24-bit colour, which every modern terminal accepts,
  produces no event at all.  The semicolon form works.  This is the single worst
  divergence from reality in the library, and it is faithful to the table.
- BEL does not terminate an OSC.  `OSC 0 ; title BEL`, which is how
  essentially every shell prompt sets the window title, leaves the parser in
  OSC_STRING forever, because the table says C0 controls are ignored there.  Only
  `ESC \` and `0x9C` close an OSC.  Fix is an opt-in flag; see PLAN.md.
- `ESC \` is reported twice over.  Closing a string with `ESC \` produces
  `OSC_END` (or `UNHOOK`) and *then* `ESC_DISPATCH '\'`, because the ESC is an
  "anywhere" transition that exits the string state and the `\` then dispatches
  from ESCAPE.  Consumers must ignore a `\` dispatch.  This is what the table
  specifies. It is not pretty.
- APC, PM and SOS payloads are discarded.  The table has no callbacks for
  them, so a Kitty graphics command vanishes entirely apart from its terminator.
- No grapheme clustering.  `vtwidth()` is per codepoint, so a ZWJ emoji
  family measures 2 per component instead of 2 total.
- No charset state.  `ESC ( 0` is reported and forgotten; DEC line-drawing
  output renders as `lqqqk`.
- Payloads over 512 bytes are truncated (`str_truncated` is set).  A Sixel
  image or a base64 Kitty payload will not fit.  The per-byte `PUT`/`OSC_PUT`
  callbacks still deliver every byte, so a consumer that needs the whole thing
  can accumulate it itself, with its own allocator, deliberately.
- 16 parameters, values clamped to 9999.  Both are the spec's stated limits.
  xterm keeps 30 and clamps at 65535, so a sequence with more than 16 parameters
  behaves differently here than in xterm.  `nparams_seen` and `param_overflow`
  report when this happened.
- The parser never gives up on a truncated sequence.  `ESC [ 1 2 ;` at end of
  input leaves the state at CSI_PARAM indefinitely.  That is correct for a stream
  and wrong for a file. A consumer that needs a timeout has to implement it.

### The wcwidth disagreements, stated plainly

103 of 65536 BMP codepoints, 12 ranges, 99.8428% agreement.  Every one is
Unicode version skew: our table is generated from the `unicodedata` in python3
3.13 (Unicode 15.1.0), and the glibc on this machine (Debian 2.41) ships
width data from **Unicode 16.0**.

| Range | vtparse | glibc | Why |
|---|---|---|---|
| U+0897 | -1 | 0 | assigned in Unicode 16.0 (a combining mark, so width 0) |
| U+1B4E..U+1B4F | -1 | 1 | assigned in 16.0 |
| U+1B7F | -1 | 1 | assigned in 16.0 |
| U+1C89..U+1C8A | -1 | 1 | assigned in 16.0 |
| U+2427..U+2429 | -1 | 1 | assigned in 16.0 |
| U+2630..U+2637 | 1 | 2 | East_Asian_Width became Wide in 16.0 (trigrams) |
| U+268A..U+268F | 1 | 2 | East_Asian_Width became Wide in 16.0 (monograms, digrams) |
| U+31E4..U+31E5 | -1 | 2 | assigned in 16.0, and Wide (CJK strokes) |
| U+3248..U+324F | 1 | 2 | East_Asian_Width Ambiguous to Wide in 16.0 |
| U+4DC0..U+4DFF | 1 | 2 | East_Asian_Width became Wide in 16.0 (Yijing hexagrams) |
| U+A7CB..U+A7CD | -1 | 1 | assigned in 16.0 |
| U+A7DA..U+A7DC | -1 | 1 | assigned in 16.0 |

So: 17 codepoints that did not exist in our Unicode version (we return -1 for
unassigned, as glibc does for codepoints it does not know) and 86 whose East
Asian width was widened by Unicode 16.0.  Nothing here is a disagreement about
what a character *is*. It is a disagreement about *when* the two tables were
built.  `make gen-width` on a newer python3 closes most of the gap and will open
a different one.  Two deliberate non-drift choices, both made to match glibc and
both defensible on their own terms: U+00AD SOFT HYPHEN is width 1, not 0,
and the Arabic prepended-concatenation marks (U+0600..U+0605 and friends) are
width 1 despite being category Cf, because Unicode excludes them from
Default_Ignorable_Code_Point.

Above the BMP the comparison is not run, because glibc's own tables and ours
disagree mostly about recently-added emoji and the result would be noise rather
than signal; the spot checks in the corpus test cover U+1F44D.

## Design

Three files, one translation unit's worth of logic.

`vtparse_lookup(state, byte)` is the table: a pure function returning an action,
a target state, and whether the cell is a transition or a stay.  It has no state
of its own, so the test can interrogate it directly, 3584 times, without running
a parser.  The engine around it is twenty lines: look up the cell, and if it is a
transition run the old state's exit action, the cell's action, then the new
state's entry action, in that order, because that is the order the spec gives.

Everything lives in the `vtparse` struct: 664 bytes, stack-allocatable, no
pointers to anything it does not own except the user's callback and cookie.  The
parameter limit (16), the value clamp (9999), the intermediate count (2) and the
payload buffer (512) exist because the struct has to have a fixed size; three of
the four numbers are the spec's own.

The "anywhere" transitions are checked before the per-state table, in every
state.  This is why C0 controls execute mid-sequence: `ESC [ 1 <CR> ; 2 m`
dispatches `SGR 1;2` *and* executes the carriage return, in that order, because
a program that interleaves a newline into its own escape sequence is a program a
terminal has to survive.  It is also why `ESC` mid-sequence abandons the
sequence instead of corrupting the next one, and why a truncated sequence
followed by a good one still works.

GR bytes (A0-FF) are *routed* like their GL twins, per the spec's note, but the
byte handed to the callback is the original: in a Latin-1 G1 set 0xE1 is
a-acute, and deciding that is the consumer's job, not the parser's.

UTF-8 mode is a single branch in the ground state, not a layer.  Bytes >= 0x80
in GROUND go to the decoder. Everywhere else they are C1 controls, which is
correct, because inside an escape sequence every byte is a control character by
definition.  When a partial character is interrupted by an ESC, the decoder
reports U+FFFD and asks the caller to re-push the byte, which then reaches the
state machine, so a write split in the middle of a character does not eat the
escape sequence that follows it.

The width table is three sorted range arrays (zero-width, wide, non-printable)
with a binary search, generated by `tools/gen_width.py` from python3's
`unicodedata` and committed.  Building vtparse needs gcc and nothing else;
`make gen-width` is a development step, not a build step.

There is exactly one `shortcut:` marker in the library, on the decision to
evaluate the table as a branch chain instead of precomputing `uint8_t[14][256]`.
The branch chain stays because it can be read line by line against the published
diagram, and that readability *is* the correctness argument.

## License

GPL-3.0-or-later.  This is a conscious choice and it has a cost worth naming: for
a *library*, the GPL is the restrictive option.  It means vtparse cannot be
linked into a proprietary terminal emulator, which is a real constraint on a real
category of consumer, and the conventional answer for a library this small and
this foundational would be LGPL-2.1, MPL-2.0, or a permissive licence.

It is GPL-3 anyway because the point of writing it was not adoption.  The
artifact that matters here is a faithful, verified encoding of a published
specification, and copyleft keeps any corrected or extended version of that
encoding available to everyone who might need it, including whoever finds the
bug in the colon sub-parameter handling.  If you want this under LGPL, the parser
is 600 lines and the table it encodes is public: ask, or re-encode it.

`LICENSE` is a verbatim copy of `/usr/share/common-licenses/GPL-3`.
