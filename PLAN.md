# PLAN

## Contract

`vtparse` is a C11 library that turns a stream of bytes into VT500-series
terminal events, plus the two Unicode pieces any consumer of those events needs
immediately: UTF-8 decoding and display width.

- `vtparse_init(&p, cb, user, utf8)` then `vtparse_push(&p, bytes, len)` (or
  `vtparse_push_byte`).  The callback receives `PRINT`, `EXECUTE`,
  `ESC_DISPATCH`, `CSI_DISPATCH`, `HOOK`/`PUT`/`UNHOOK` and
  `OSC_START`/`OSC_PUT`/`OSC_END`, with parameters, intermediates and the
  accumulated string payload readable from the struct.
- `vtutf8_push()` decodes bytes to codepoints with one U+FFFD per maximal
  ill-formed subpart; `vtwidth(cp)` returns -1/0/1/2 like `wcwidth(3)`.
- `vtparse` is a CLI that prints one line per event, which is what makes all of
  the above testable from a shell.
- Not in the contract: drawing, a screen model, terminfo, a pty, an allocator.

## Hard invariant

The parser never allocates, never reads past the byte it was given, never
rejects input, and its state transitions match the table published at
<https://vt100.net/emu/dec_ansi_parser> for every (state, byte) pair: all
14 x 256 = 3584 of them.

Everything else follows from that invariant.  No allocation means every buffer
is fixed and inline, which means `VTPARSE_MAX_PARAMS` (16), `VTPARSE_PARAM_MAX`
(9999), two intermediates and a 512-byte string payload are not arbitrary
ceilings but the spec's own stated limits turned into struct fields.  Never
reading ahead means the parser is correct across any chunking of the input,
including one byte per call, which the corpus test asserts for every case.
Never rejecting means a hostile byte stream changes the event stream and nothing
else, there is no error path to get wrong.

(The brief asked for 4096 pairs on the assumption of 16 states.  The published
table has 14; 3584 is the honest number.  See README "Status".)

## Oracle

Ranked by how much each one would catch, strongest first.

1. The published state table.  `t/table_test.c` is an independent,
   declarative transcription of the vt100.net table ("00-17,19,1C-1F /
   execute"), deliberately in a different shape from the if/switch chain in
   `vtparse.c`.  It asserts the spec table is total, that `vtparse_lookup()`
   agrees with it on all 3584 pairs down to action and transition-vs-stay, and
   that the *live* parser, driven into each state with a real byte prefix,
   lands where the table says and fires the callback the table says.  This is
   the only oracle that is a genuine external specification, not a
   comparison against another implementation's opinion.
2. CPython's UTF-8 decoder.  `t/utf8_diff.py` decodes 88,954 generated
   sequences, valid and invalid, with our decoder and with
   `bytes.decode('utf-8', errors='replace')`, and compares codepoint sequences.
   Invalid input is where decoders disagree, and the exhaustive three-byte sweep
   plus the TR#36 examples pin down the maximal-subpart rule exactly.
3. libc `wcwidth(3)`.  `t/width_diff.c` compares `vtwidth()` against glibc
   over the whole BMP in a UTF-8 locale.  Not a perfect oracle: the two tables
   come from different Unicode versions and the drift is real, so the test
   reports the disagreement ranges instead of hiding them, and fails only on a
   collapse large enough to mean a table bug.
4. A corpus of sequences real programs emit.  `t/corpus_test.c`: SGR colour
   in three forms, cursor movement, OSC title and hyperlink, bracketed paste,
   DECSET/DECRST, DECRQSS, a Sixel header, a Kitty graphics APC, C0 controls
   interleaved mid-sequence, truncation, 20 parameters and a clamped value.
   Each asserts the exact event stream including per-byte payload counts.

Secondary: `make test-asan` (`-fsanitize=address,undefined`) and
`make test-valgrind` over 400kB of `/dev/urandom` in both parser modes, because
"never allocates, never reads past the byte it was given" is a claim a
sanitizer can check and a reviewer cannot.

## Non-goals

- Rendering.  No screen buffer, no cursor, no scrollback, no ncurses, no pty.
  The library tells you what happened. What to do about it is the consumer's
  problem.
- Interpreting the sequences.  `CSI_DISPATCH 'm'` is reported, not applied;
  vtparse does not know what SGR 38 means.
- ICU, or any library.  The width table is built in; `tools/gen_width.py` is a
  development tool whose output is committed.
- Grapheme clustering.  Width is per codepoint, so a ZWJ emoji family reports
  the sum of its parts.  This is wrong for rendering and right for the layer
  below rendering; see Milestone 1.
- Charset state (SCS, G0-G3, the DEC line-drawing set).  `ESC ( 0` is reported
  faithfully and then forgotten.
- Being bug-compatible with xterm where xterm departs from the published table.
  Where the two differ, vtparse follows the table and documents the difference.

## Commit sequence

- [x] the callback API, the ground state and the escape states
      - why: the table is the only part with an external specification to be
        right or wrong against, so it goes in first and alone.
- [x] the four CSI states, then parameter storage and the spec's two clamps
      - why: split because the routing and the accumulation fail differently,
        and a bisect that lands between them says which.
- [x] OSC_STRING and the payload buffer, then the five DCS states, then SOS/PM/APC
      - why: one commit per construct, each with the divergence it introduces
        written into its own message, not discovered later.
- [x] the 8-bit C1 introducers and GR folding
      - why: last, because every state they can reach has to exist first.
- [x] t/run.sh and a golden CLI event stream
      - why: a test nobody can run is not a test, and every oracle after this
        is two lines in a script that already works.
- [x] the table-driven oracle over all 3584 (state, byte) pairs
      - why: written from the published table, not from the code, so
        disagreement means one of them is wrong, not both agreeing.
- [x] the corpus, UTF-8 decoding, the width table, then the two differential
      oracles
      - why: wcwidth and CPython are the only external references available
        without a network, and a corpus of real sequences catches the
        plausible-but-wrong behaviour that a table test cannot see.
- [x] the sanitizer and valgrind targets
      - why: the no-allocation claim is checkable by a tool and not by a reader.
- [x] README and PLAN
      - why: the wcwidth disagreement list is a result, not a footnote, and it
        has to be written down where a consumer will read it.

### Milestone 1: the things a terminal actually needs next

- [ ] grapheme cluster segmentation (UAX #29) with a `vtwidth_cluster()` entry
      point
      - why: per-codepoint width is wrong for every flag, skin-tone emoji and
        Devanagari syllable, and consumers currently have to guess.
      - oracle, located: `GraphemeBreakTest.txt` is the Unicode Consortium's
        own conformance file and it is on this machine, at
        `/run/host/usr/share/unicode/ucd/auxiliary/`. 853 cases, each one a
        sequence of code points with every boundary marked, written by the
        people who wrote the rules. That is a stronger authority than this repo
        has for anything else, including the DEC table.
      - the data the rules need is there too, all at 18.0.0:
        `auxiliary/GraphemeBreakProperty.txt` for the main property,
        `emoji/emoji-data.txt` for `Extended_Pictographic` (GB11, 456 ranges),
        and `DerivedCoreProperties.txt` for `Indic_Conjunct_Break` (GB9c, 516
        lines), which is the rule added in 15.1 and the one most
        implementations get wrong.
      - the blocker is a version conflict instead of missing data. `width_table.h`
        says it was generated from "Unicode 15.1.0, as shipped with the python3
        that ran the generator", and the UCD files here are 18.0.0. Generating
        a grapheme table from 18.0.0 next to a width table from 15.1.0 gives
        one library two Unicode versions, and the conformance file would then
        be testing one of them against data from the other. So this item has to
        start by deciding which version the repo targets and regenerating the
        width table to match, which is its own change, with its own
        3584-pair re-verification against the DEC table, and should be the
        commit before this one.
      - note that the UCD lives on a flatpak host mount, not in this
        container's own filesystem, so the generated tables must be committed
        (as `width_table.h` already is) and the harness must skip loudly if the
        corpus is absent, the way jail's does for Landlock.
- [ ] sub-parameter support, so `SGR 38:2::10:20:30` dispatches instead of
      going to CSI_IGNORE
      - why: the published table predates ECMA-48 colon sub-parameters, and
        truecolour in the colon form is now common enough that silently
        dropping it is the worst failure mode in the library.
- [ ] an opt-in BEL-terminates-OSC mode
      - why: every shell prompt on earth sets its title with
        `OSC 0 ; title BEL`, and the strict table leaves the parser stuck in
        OSC_STRING forever.
- [ ] APC/PM/SOS payload callbacks, then a Kitty graphics protocol decoder on
      top (key/value header, base64 payload, chunked `m=1` continuation)
      - why: the spec discards these strings entirely, so image protocols are
        invisible to the current API.
- [ ] Sixel decoding: the DCS `q` payload into a raster of indexed pixels
      - why: the passthrough bytes are already delivered; nothing interprets
        the colour registers, repeat introducer or raster attributes.
- [ ] a vttest conformance comparison table across xterm, foot and alacritty
      - why: a per-sequence agree/differ matrix against three implementations
        is a far stronger claim than "it matches a published table", and it
        turns every divergence in the Status list into a measured number.
- [ ] replace the if/switch lookup with a generated `uint8_t[14][256]` table and
      benchmark both
      - why: the branch chain is the one performance shortcut in the hot path
        and the only `shortcut:` marker in the library.
- [ ] a `-DVTPARSE_FUZZ` libFuzzer entry point and a seed corpus
      - why: 400kB of `/dev/urandom` per run is a smoke test, not coverage-
        guided fuzzing, and this code exists to eat hostile input.
- [ ] regenerate the width table from UnicodeData.txt directly instead of via
      python3's `unicodedata`
      - why: the Unicode version would then be pinned by the repo,
        not by whichever python3 ran the generator, which is the cause of the
        entire wcwidth disagreement list.
- [ ] UTF-8 encode, and a width-aware column-truncation helper
      - why: every consumer of this library writes both of those itself within
        a day.
- [ ] DEC charset mapping (SCS plus the line-drawing set) behind a flag
      - why: `ESC ( 0` is reported and then ignored, so box-drawing output from
        anything older than Unicode renders as ASCII letters.
