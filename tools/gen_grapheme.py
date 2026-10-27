#!/usr/bin/env python3
"""Generate grapheme_table.h from the Unicode Character Database.

Run by hand (`make gen-grapheme`), NOT at build time: the generated header is
committed so that building vtparse needs nothing but gcc, and the UCD is not
in this repository.

WHY generate rather than type.  UAX #29 needs three properties and they come
to roughly 2700 ranges between them.  Typed by hand that is 2700 chances to be
wrong in a place no test looks, and the places nobody looks are exactly the
scripts nobody on this machine reads.  The sibling repo bpec makes the same
argument about its encoder alphabet.  So the ranges are read out of the UCD,
the Unicode version is read out of the UCD, and both are printed into the
header where a reader can see which snapshot produced it.

Three files, which must all be the same version as the one width_table.h was
built from:

  auxiliary/GraphemeBreakProperty.txt  Grapheme_Cluster_Break, rules GB3-GB9b
                                       and GB12/GB13
  emoji/emoji-data.txt                 Extended_Pictographic, rule GB11;
                                       and Emoji, which UAX #29 does not use
                                       but vtwidth_cluster() does, because
                                       an emoji presentation sequence per
                                       UTS #51 is an Emoji=Yes character
                                       followed by U+FE0F and U+0023 is
                                       Emoji=Yes while not being
                                       Extended_Pictographic
  DerivedCoreProperties.txt            Indic_Conjunct_Break, rule GB9c

One byte per range holds all four:

  bits 0-3   Grapheme_Cluster_Break class (0 = Other)
  bits 4-5   Indic_Conjunct_Break        (0 = None)
  bit  6     Extended_Pictographic
  bit  7     Emoji

Code points with all four at their default are left out of the table
entirely, which is most of them.
"""
import os
import re
import sys

MAX = 0x110000

UCD_DIRS = (
    os.environ.get('VTPARSE_UCD'),
    '/run/host/usr/share/unicode/ucd',
    '/usr/share/unicode/ucd',
)

# Order is the C enum's order and the generated #defines carry it across, so
# this list is the single place it is written down.
GCB = ['Other', 'CR', 'LF', 'Control', 'Extend', 'ZWJ', 'Regional_Indicator',
       'Prepend', 'SpacingMark', 'L', 'V', 'T', 'LV', 'LVT']
INCB = ['None', 'Consonant', 'Extend', 'Linker']

LINE = re.compile(r'^([0-9A-Fa-f]+)(?:\.\.([0-9A-Fa-f]+))?\s*;\s*([^#]*)')


def ucd_dir():
    for d in UCD_DIRS:
        if d and os.path.exists(os.path.join(d, 'auxiliary',
                                             'GraphemeBreakProperty.txt')):
            return d
    sys.exit("no UCD found; set VTPARSE_UCD to a directory holding "
             "auxiliary/GraphemeBreakProperty.txt, emoji/emoji-data.txt and "
             "DerivedCoreProperties.txt")


def version(path):
    with open(path, encoding='utf-8') as f:
        head = f.read(2000)
    m = re.search(r'-(\d+\.\d+\.\d+)\.txt', head) or \
        re.search(r'Version:\s*(\d+\.\d+\.\d+)', head)
    if not m:
        sys.exit("cannot read a Unicode version out of %s" % path)
    return m.group(1)


def parse(path, want):
    """Yield (lo, hi, value) for every line whose first property field is in
    `want`.  DerivedCoreProperties.txt spells Indic_Conjunct_Break as two
    fields, `InCB; Linker`, so the field is split and rejoined."""
    out = []
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.split('#')[0]
            m = LINE.match(line)
            if not m:
                continue
            lo = int(m.group(1), 16)
            hi = int(m.group(2), 16) if m.group(2) else lo
            fields = [x.strip() for x in m.group(3).split(';') if x.strip()]
            key = ';'.join(fields)
            if key in want:
                out.append((lo, hi, want[key]))
    return out


def main():
    d = ucd_dir()
    gbp = os.path.join(d, 'auxiliary', 'GraphemeBreakProperty.txt')
    emo = os.path.join(d, 'emoji', 'emoji-data.txt')
    dcp = os.path.join(d, 'DerivedCoreProperties.txt')
    ver = version(gbp)
    for p in (emo, dcp):
        if version(p) != ver:
            sys.exit("UCD version mismatch: %s is %s, %s is %s"
                     % (gbp, ver, p, version(p)))

    flags = bytearray(MAX)
    counts = {}

    for lo, hi, v in parse(gbp, {n: i for i, n in enumerate(GCB) if i}):
        counts.setdefault('gcb', 0)
        counts['gcb'] += 1
        for cp in range(lo, hi + 1):
            flags[cp] |= v

    for lo, hi, v in parse(emo, {'Extended_Pictographic': 0x40,
                                 'Emoji': 0x80}):
        counts.setdefault('pict' if v == 0x40 else 'emoji', 0)
        counts['pict' if v == 0x40 else 'emoji'] += 1
        for cp in range(lo, hi + 1):
            flags[cp] |= v

    for lo, hi, v in parse(dcp, {'InCB;' + n: i
                                 for i, n in enumerate(INCB) if i}):
        counts.setdefault('incb', 0)
        counts['incb'] += 1
        for cp in range(lo, hi + 1):
            flags[cp] |= v << 4

    # Collapse to runs of equal flags, dropping the all-default runs.
    rows = []
    cp = 0
    while cp < MAX:
        f = flags[cp]
        start = cp
        while cp + 1 < MAX and flags[cp + 1] == f:
            cp += 1
        if f:
            rows.append((start, cp, f))
        cp += 1

    path = sys.argv[1] if len(sys.argv) > 1 else 'grapheme_table.h'
    with open(path, 'w') as f:
        print("/* Generated by tools/gen_grapheme.py -- do not edit.", file=f)
        print(" * Unicode %s, from auxiliary/GraphemeBreakProperty.txt,"
              % ver, file=f)
        print(" * emoji/emoji-data.txt and DerivedCoreProperties.txt.", file=f)
        print(" * Same UCD snapshot as width_table.h; keep them equal. */",
              file=f)
        print('#define VTGB_UNICODE_VERSION "%s"' % ver, file=f)
        for i, n in enumerate(GCB):
            print("#define VTGB_%-18s %d" % (n.upper(), i), file=f)
        for i, n in enumerate(INCB):
            print("#define VTGB_INCB_%-13s %d" % (n.upper(), i), file=f)
        print("#define VTGB_PICT_BIT        0x40", file=f)
        print("#define VTGB_EMOJI_BIT       0x80", file=f)
        print("struct vtgb_range { uint32_t lo, hi; uint8_t f; };", file=f)
        print("static const struct vtgb_range vtgb_tbl[] = {", file=f)
        for lo, hi, fl in rows:
            print("\t{0x%04X, 0x%04X, 0x%02X}," % (lo, hi, fl), file=f)
        print("};", file=f)

    sys.stderr.write("unicode %s (ucd %s): %d GCB lines, %d ExtPict lines, "
                     "%d Emoji lines, %d InCB lines -> %d table ranges\n"
                     % (ver, d, counts.get('gcb', 0), counts.get('pict', 0),
                        counts.get('emoji', 0), counts.get('incb', 0),
                        len(rows)))


if __name__ == '__main__':
    main()
