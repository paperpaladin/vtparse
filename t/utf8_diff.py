#!/usr/bin/env python3
"""ORACLE 3: differential UTF-8 decode against CPython.

Generates valid and invalid byte sequences, decodes each with t/utf8_dump (our
decoder) and with bytes.decode('utf-8', errors='replace') (CPython's), and
compares the codepoint sequences.

Invalid-sequence handling is where decoders differ, so the policy has to be
stated rather than assumed.  Ours, and CPython's, is the Unicode 15.0 "Best
Practices for Using U+FFFD" rule: exactly one U+FFFD per *maximal subpart* of an
ill-formed subsequence.  So b'\\xe0\\x80\\x80' is three U+FFFD (0xE0 can only be
followed by A0-BF, so the subpart is one byte long and each trailing byte is its
own subpart), while b'\\xf0\\x9f\\x92' is a single U+FFFD (a truncated but
so-far-well-formed prefix is one subpart).  A decoder that emitted one U+FFFD
per sequence, or one per byte, would disagree here -- that is the point of the
test.

Prints "ok"/"not ok" lines for t/run.sh to count.
"""
import os
import random
import subprocess
import sys

DUMP = sys.argv[1] if len(sys.argv) > 1 else './t/utf8_dump'

CONT_SAMPLE = [0x00, 0x0A, 0x41, 0x7F, 0x80, 0x8F, 0x9F, 0xA0, 0xBF, 0xC0,
               0xE0, 0xF0, 0xFF]


def cases():
    """Yield (group name, bytes) pairs.  Structured first, then fuzz."""
    name = 'every single byte'
    for b in range(256):
        yield name, bytes([b])

    name = 'two-byte: every lead x sampled second byte'
    for lead in range(0x80, 0x100):
        for second in CONT_SAMPLE:
            yield name, bytes([lead, second])

    name = 'two-byte: every valid 2-byte sequence'
    for lead in range(0xC2, 0xE0):
        for second in range(0x80, 0xC0):
            yield name, bytes([lead, second])

    name = 'three-byte: every lead x sampled continuations'
    for lead in range(0xE0, 0xF0):
        for b2 in CONT_SAMPLE:
            for b3 in CONT_SAMPLE:
                yield name, bytes([lead, b2, b3])

    name = 'three-byte: exhaustive E0-EF x 80-BF x 80-BF'
    for lead in range(0xE0, 0xF0):
        for b2 in range(0x80, 0xC0):
            for b3 in range(0x80, 0xC0):
                yield name, bytes([lead, b2, b3])

    name = 'three-byte: boundary continuations'
    for lead in range(0xE0, 0xF0):
        for b2 in (0x80, 0x9F, 0xA0, 0xBF):
            for b3 in (0x80, 0xBF):
                yield name, bytes([lead, b2, b3])

    name = 'four-byte: every lead x boundary continuations'
    for lead in range(0xF0, 0x100):
        for b2 in (0x80, 0x8F, 0x90, 0xBF, 0xC0):
            for b3 in (0x80, 0xBF, 0x41):
                for b4 in (0x80, 0xBF, 0x41):
                    yield name, bytes([lead, b2, b3, b4])

    name = 'known hard cases'
    for raw in [
        b'\xc0\x80', b'\xc1\xbf',                  # overlong 2-byte
        b'\xe0\x80\x80', b'\xe0\x9f\xbf',          # overlong 3-byte
        b'\xf0\x80\x80\x80', b'\xf0\x8f\xbf\xbf',  # overlong 4-byte
        b'\xed\xa0\x80', b'\xed\xbf\xbf',          # UTF-16 surrogates
        b'\xed\xa0\x80\xed\xb0\x80',               # CESU-8 pair
        b'\xf4\x90\x80\x80', b'\xf5\x80\x80\x80',  # above U+10FFFF
        b'\xfe', b'\xff', b'\xfe\xff',             # never-legal bytes
        b'\xe1\x80', b'\xf0\x9f\x92',              # truncated, well-formed so far
        b'\xf1\x80\x80\xe1\x80\xc0',               # Unicode TR#36 example
        b'\x61\xf1\x80\x80\xe1\x80\xc0\x62',       # ditto, with surrounding text
        b'\xef\xbb\xbfhello',                      # BOM
        b'\xc3\xa9\xcc\x81',                       # e-acute + combining
        b'\xe6\x97\xa5\xe6\x9c\xac',               # CJK
        b'\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd',       # emoji + skin tone modifier
        b'\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x92\xbb',  # ZWJ sequence
        b'\x1b[31m\xc3\xa9',                       # escape sequence bytes
        b'\x80\x80\x80',                           # bare continuations
        b'\xc2',                                   # lone lead at EOF
    ]:
        yield name, raw

    name = 'fuzz: random bytes'
    rnd = random.Random(20261005)
    for _ in range(8000):
        n = rnd.randrange(1, 9)
        yield name, bytes(rnd.randrange(256) for _ in range(n))

    name = 'fuzz: random bytes biased to lead/continuation bytes'
    pool = list(range(0xC0, 0xF8)) + list(range(0x80, 0xC0)) + [0x41, 0x1B]
    for _ in range(8000):
        n = rnd.randrange(1, 9)
        yield name, bytes(rnd.choice(pool) for _ in range(n))


def main():
    if not os.path.exists(DUMP):
        print('not ok - %s missing' % DUMP)
        return 1
    names, raws = [], []
    for name, raw in cases():
        names.append(name)
        raws.append(raw)

    stdin = ''.join(r.hex() + '\n' for r in raws)
    res = subprocess.run([DUMP], input=stdin, capture_output=True, text=True)
    if res.returncode != 0:
        print('not ok - %s exited %d: %s'
              % (DUMP, res.returncode, res.stderr.strip()))
        return 1
    got = res.stdout.split('\n')

    per_group = {}
    examples = {}
    for i, raw in enumerate(raws):
        want = ' '.join('%04X' % ord(c)
                        for c in raw.decode('utf-8', errors='replace'))
        mine = got[i] if i < len(got) else '<missing>'
        g = per_group.setdefault(names[i], [0, 0])
        if mine == want:
            g[0] += 1
        else:
            g[1] += 1
            if names[i] not in examples:
                examples[names[i]] = (raw.hex(), want, mine)

    fail = 0
    total_ok = total_bad = 0
    for name in dict.fromkeys(names):
        good, bad = per_group[name]
        total_ok += good
        total_bad += bad
        print('%sok - %s: %d/%d match CPython'
              % ('' if bad == 0 else 'not ', name, good, good + bad))
        if bad:
            fail = 1
            h, want, mine = examples[name]
            print('#   e.g. %s: cpython=[%s] vtparse=[%s]' % (h, want, mine))
    print('# %d cases, %d agree, %d differ' % (total_ok + total_bad,
                                               total_ok, total_bad))
    print('%sok - %d/%d sequences decode identically to CPython'
          % ('' if total_bad == 0 else 'not ', total_ok, total_ok + total_bad))
    return fail


if __name__ == '__main__':
    sys.exit(main())
