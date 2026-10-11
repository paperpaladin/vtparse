#!/usr/bin/env python3
"""Generate width_table.h from the Unicode Character Database.

Run by hand (`make gen-width`), NOT at build time: the generated header is
committed so that building vtparse needs nothing but gcc.  Re-run this when you
want to move to a newer Unicode version, then re-run `make test` and update the
disagreement ranges quoted in README.md -- they WILL change.

This used to read python3's built-in `unicodedata`, which meant the Unicode
version of the table was whatever the interpreter happened to ship (15.1.0 on
the machine that first ran it) and nothing in the repo recorded or controlled
it.  It now reads UnicodeData.txt and EastAsianWidth.txt directly, so the
version is pinned by the files on disk and printed into the header.  That also
makes it possible to generate the width table and the grapheme table
(tools/gen_grapheme.py) from one UCD snapshot instead of two.

The rules below are chosen to match glibc's wcwidth(3), because glibc is our
differential oracle (t/width_diff.c).  glibc builds its own table from
UnicodeData.txt + EastAsianWidth.txt the same way, so where we still disagree
the cause is almost always that the two tables were generated from different
Unicode versions.  See README.md "Status".

Self-check: the old rule is still implemented, against python3's `unicodedata`,
and the generator compares the two tables and reports the disagreement.  A
parsing bug in the UCD reader (the First>/Last> range convention in
UnicodeData.txt is the easy one to get wrong, and it covers all of plane 2,
which t/width_diff.c does not reach) shows up there as thousands of
differences instead of the couple of hundred that a version bump costs.
"""
import os
import re
import sys
import unicodedata

MAX = 0x110000

UCD_DIRS = (
    os.environ.get('VTPARSE_UCD'),
    '/run/host/usr/share/unicode/ucd',
    '/usr/share/unicode/ucd',
)

# glibc gives these width 1 even though they are category Cf.  They are the
# "prepended concatenation marks" (Arabic number signs, which really do occupy
# a column) and the interlinear annotation controls.  Not a fudge: Unicode
# itself excludes them from Default_Ignorable_Code_Point.
CF_WIDTH_ONE = set(range(0x0600, 0x0606)) | {
    0x06DD, 0x070F, 0x0890, 0x0891, 0x08E2, 0x110BD, 0x110CD,
    0xFFF9, 0xFFFA, 0xFFFB,
    0x00AD,   # SOFT HYPHEN: glibc renders it as a visible hyphen.
}

# Conjoining Hangul jamo: a medial vowel or final consonant composes onto the
# preceding leading consonant, so it adds no columns of its own.  Category is
# Lo, so the Mn/Me rule does not catch them.
JAMO_ZERO = (
    (0x1160, 0x11FF),   # Jamo medial vowels + final consonants
    (0xD7B0, 0xD7C6),   # Jamo Extended-B medial vowels
    (0xD7CB, 0xD7FB),   # Jamo Extended-B final consonants
)
FILLER_ZERO = {0x3164, 0xFFA0}  # HANGUL FILLER, HALFWIDTH HANGUL FILLER


def ucd_dir():
    for d in UCD_DIRS:
        if d and os.path.exists(os.path.join(d, 'UnicodeData.txt')):
            return d
    sys.exit("no UCD found; set VTPARSE_UCD to a directory holding "
             "UnicodeData.txt and EastAsianWidth.txt")


def ucd_version(d):
    """The UCD does not state its version in UnicodeData.txt, so take it from
    a file that does.  Every auxiliary file names it on line 1."""
    for name in ('auxiliary/GraphemeBreakProperty.txt', 'emoji/emoji-data.txt'):
        p = os.path.join(d, name)
        if not os.path.exists(p):
            continue
        with open(p, encoding='utf-8') as f:
            head = f.read(2000)
        m = re.search(r'-(\d+\.\d+\.\d+)\.txt', head) or \
            re.search(r'Version:\s*(\d+\.\d+\.\d+)', head)
        if m:
            return m.group(1)
    sys.exit("cannot determine the UCD version from %s" % d)


def read_categories(d):
    """General_Category per code point, 'Cn' for unassigned.

    UnicodeData.txt spells large blocks as a pair of lines whose names end
    in ', First>' and ', Last>'; everything between them has the properties
    of the First line.  Miss that and all of CJK Extension B reads as
    unassigned."""
    cat = ['Cn'] * MAX
    first = None
    with open(os.path.join(d, 'UnicodeData.txt'), encoding='utf-8') as f:
        for line in f:
            fields = line.split(';')
            if len(fields) < 3:
                continue
            cp = int(fields[0], 16)
            name, c = fields[1], fields[2]
            if name.endswith(', First>'):
                first = (cp, c)
                continue
            if name.endswith(', Last>'):
                assert first is not None and first[1] == c, line
                for x in range(first[0], cp + 1):
                    cat[x] = c
                first = None
                continue
            cat[cp] = c
    assert first is None, "unterminated First> range in UnicodeData.txt"
    return cat


def read_east_asian_width(d):
    """East_Asian_Width per code point.

    The @missing lines matter: unassigned code points in the CJK and plane
    2/3 ideograph blocks default to W, not N.  They are all category Cn and
    so come out non-printable anyway, but parsing them keeps this function
    honest on its own terms."""
    eaw = ['N'] * MAX
    pat = re.compile(r'^([0-9A-F]+)(?:\.\.([0-9A-F]+))?\s*;\s*(\w+)')
    with open(os.path.join(d, 'EastAsianWidth.txt'), encoding='utf-8') as f:
        for line in f:
            m = re.match(r'#\s*@missing:\s*([0-9A-F]+)\.\.([0-9A-F]+)\s*;\s*(\w+)',
                         line)
            if m:
                lo, hi, v = int(m.group(1), 16), int(m.group(2), 16), m.group(3)
                for x in range(lo, min(hi, MAX - 1) + 1):
                    eaw[x] = v
                continue
            line = line.split('#')[0].strip()
            if not line:
                continue
            m = pat.match(line)
            if not m:
                continue
            lo = int(m.group(1), 16)
            hi = int(m.group(2), 16) if m.group(2) else lo
            v = m.group(3)
            for x in range(lo, min(hi, MAX - 1) + 1):
                eaw[x] = v
    return eaw


def width_from(cp, cat, eaw):
    if cp == 0:
        return 0            # POSIX: wcwidth(0) == 0, not -1.
    if cat in ('Cc', 'Cs', 'Cn', 'Zl', 'Zp'):
        return -1           # not printable
    if cat == 'Cf':
        return 1 if cp in CF_WIDTH_ONE else 0
    if cat in ('Mn', 'Me'):
        return 0            # combining marks stack on the base glyph
    if any(a <= cp <= b for a, b in JAMO_ZERO) or cp in FILLER_ZERO:
        return 0
    if eaw in ('W', 'F'):
        return 2            # East Asian Wide/Fullwidth; covers emoji, which
                            # EastAsianWidth.txt marks W since Unicode 9.
    return 1


def ranges(w, want):
    out = []
    cp = 0
    while cp < MAX:
        if w[cp] == want:
            start = cp
            while cp + 1 < MAX and w[cp + 1] == want:
                cp += 1
            out.append((start, cp))
        cp += 1
    return out


def emit(f, name, rs):
    print("static const struct vtw_range %s[] = {" % name, file=f)
    for a, b in rs:
        print("\t{0x%04X, 0x%04X}," % (a, b), file=f)
    print("};", file=f)


def selfcheck(w, cat):
    """Compare against the rule as it was when it read python3's unicodedata.

    A bare difference count is useless here: moving from the interpreter's
    15.1.0 to the UCD's 18.0.0 changes 23184 code points, almost all of them
    newly assigned, and that number on its own cannot be told apart from a
    broken reader.  So the differences are split three ways:

      newly assigned  -- Cn in python, assigned in the UCD.  Expected, and
                         the bulk of it (CJK Extension J alone is 15626).
      un-assigned     -- assigned in python, Cn in the UCD.  Unicode does
                         not do this, so any count here is a reader bug,
                         and specifically the First>/Last> one.
      both assigned   -- a genuine property change, e.g. the trigrams at
                         U+2630..U+2637 became East Asian Wide in 16.0.
    """
    new, gone, changed, sample = 0, 0, 0, []
    for cp in range(MAX):
        c = chr(cp)
        pycat = unicodedata.category(c)
        if width_from(cp, pycat, unicodedata.east_asian_width(c)) == w[cp]:
            continue
        if pycat == 'Cn':
            new += 1
        elif cat[cp] == 'Cn':
            gone += 1
        else:
            changed += 1
            if len(sample) < 5:
                sample.append("U+%04X" % cp)
    return new, gone, changed, sample


def main():
    d = ucd_dir()
    ver = ucd_version(d)
    cat = read_categories(d)
    eaw = read_east_asian_width(d)
    w = [width_from(cp, cat[cp], eaw[cp]) for cp in range(MAX)]
    zero = ranges(w, 0)
    wide = ranges(w, 2)
    nonpr = ranges(w, -1)
    with open(sys.argv[1] if len(sys.argv) > 1 else 'width_table.h', 'w') as f:
        print("/* Generated by tools/gen_width.py -- do not edit.", file=f)
        print(" * Unicode %s, from UnicodeData.txt and EastAsianWidth.txt."
              % ver, file=f)
        print(" * Same UCD snapshot as grapheme_table.h; keep them equal. */",
              file=f)
        print('#define VTW_UNICODE_VERSION "%s"' % ver, file=f)
        print("struct vtw_range { uint32_t lo, hi; };", file=f)
        emit(f, "vtw_zero", zero)
        emit(f, "vtw_wide", wide)
        emit(f, "vtw_nonprint", nonpr)
    new, gone, changed, sample = selfcheck(w, cat)
    sys.stderr.write("zero=%d wide=%d nonprint=%d ranges, unicode %s (ucd %s)\n"
                     % (len(zero), len(wide), len(nonpr), ver, d))
    sys.stderr.write("selfcheck vs python3 unicodedata %s: %d newly assigned,"
                     " %d un-assigned (must be 0), %d changed while assigned"
                     " in both%s\n"
                     % (unicodedata.unidata_version, new, gone, changed,
                        (" (" + ", ".join(sample) + ", ...)") if sample else ""))
    if gone:
        sys.exit("UCD reader bug: %d code points went from assigned to"
                 " unassigned" % gone)


if __name__ == '__main__':
    main()
