"""Checks the byte patterns the plugin searches tcnyc.exe for, the way the plugin uses them:
- the key-name pattern matches once, its call leads to the button-to-action function (whose first
  9 bytes the plugin also checks), and its first operand is the current-control-set variable;
- the quit prompt's key check matches (the plugin accepts 1-4 copies) and every copy uses one flag;
- the quit picture's file name matches once, and the push-and-call that uses it matches once;
- the loading screen's three text calls match once each and reach one function, and the screen-size
  pattern leads to a `mov eax,[height]; ret` getter.
On the build the plugin was developed on, the addresses found must be the known ones.
The patterns and the 9 bytes are read from the C source, so anything edited there is checked as edited.
Needs the game's tcnyc.exe (path as argument, TCNYC_EXE, or one folder up). Without it: exit 2
(reported as SKIP)."""
import hashlib, os, re, struct, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
exe = sys.argv[1] if len(sys.argv) > 1 else os.environ.get('TCNYC_EXE', os.path.join(root, '..', 'tcnyc.exe'))
if not os.path.exists(exe):
    print(f'NOT RUN: no tcnyc.exe at {exe}')
    sys.exit(2)
src = open(os.path.join(root, 'tcnyc_sdl3pad.c'), encoding='utf-8').read()
d = open(exe, 'rb').read()
known = hashlib.md5(d).hexdigest() == 'b7eee2f3f4c2014d235acf238716b495'

pe = struct.unpack('<I', d[0x3C:0x40])[0]
nsec = struct.unpack('<H', d[pe + 6:pe + 8])[0]
opt = struct.unpack('<H', d[pe + 20:pe + 22])[0]
base = struct.unpack('<I', d[pe + 24 + 28:pe + 24 + 32])[0]
secs = []
for i in range(nsec):
    o = pe + 24 + opt + i * 40
    vs, va, rs, ra = struct.unpack('<4I', d[o + 8:o + 24])
    secs.append((va, vs, ra, rs, struct.unpack('<I', d[o + 36:o + 40])[0]))

def scan(pat, code):
    rx = b''.join(b'.' if t.startswith('?') else re.escape(bytes([int(t, 16)])) for t in pat.split())
    hits = []
    for va, vs, ra, rs, ch in secs:
        if code and not ch & 0x20000000:
            continue
        hits += [base + va + m.start() for m in re.finditer(rx, d[ra:ra + min(vs, rs)], re.S)]
    return hits

def read(va, n=4):
    for sva, vs, ra, rs, ch in secs:
        if base + sva <= va < base + sva + vs:
            o = va - base - sva + ra
            return d[o:o + n]
    return b''

def literal(function):   # the first byte pattern written inside the named C function
    m = re.search(r'(?s)%s.*?"((?:[0-9A-F]{2}|\?\?)(?: (?:[0-9A-F]{2}|\?\?)){8,})"' % re.escape(function), src)
    return m.group(1) if m else None

fails = []
def check(ok, what):
    print(('ok    ' if ok else 'FAIL  ') + what)
    if not ok:
        fails.append(what)

# button prompts
sig = literal('static void install_prompts(void)')
hits = scan(sig, True) if sig else []
check(len(hits) == 1, f'key-name pattern matches once ({len(hits)})')
if len(hits) == 1:
    p = hits[0]
    curset = struct.unpack('<I', read(p + 1))[0]
    m2a = p + 36 + struct.unpack('<i', read(p + 32))[0]
    m = re.search(r'm2a\[9\] = \{([^}]*)\}', src)
    want = bytes(int(x, 16) for x in m.group(1).split(',')) if m else b''
    check(len(want) == 9 and read(m2a, 9) == want, f'button-to-action function at {m2a:#x} starts with the 9 bytes the C source expects')
    if known:
        check((p, curset, m2a) == (0x63ED90, 0x75CCC0, 0x63EAF0), f'known build: {p:#x} / {curset:#x} / {m2a:#x}')

# quit prompt key checks
sig = literal('static void install_quit_screen(void)')
hits = scan(sig, True) if sig else []
flags = {struct.unpack('<I', read(h + 2))[0] for h in hits}
check(1 <= len(hits) <= 4 and len(flags) == 1, f'quit key check: {len(hits)} copies, flags {[hex(f) for f in flags]}')
if known:
    check(len(hits) == 2 and flags == {0x793359}, 'known build: 2 copies using 0x793359')

# quit picture
name = scan('21 53 48 45 4C 4C 21 5C 51 75 69 74 47 61 6D 65 2E 70 63 74 00', False)
check(len(name) == 1, f'"!SHELL!\\QuitGame.pct" found once ({len(name)})')
check('21 53 48 45 4C 4C 21 5C 51 75 69 74 47 61 6D 65 2E 70 63 74 00' in src, 'the C source searches for the same file name')
if len(name) == 1:
    a = name[0]
    push = '68 %02X %02X %02X %02X C7 05 ?? ?? ?? ?? 01 00 00 00 E8' % (a & 255, a >> 8 & 255, a >> 16 & 255, a >> 24)
    check('"68 %02X %02X %02X %02X C7 05 ?? ?? ?? ?? 01 00 00 00 E8"' in src, 'the C source builds the same push-and-call pattern')
    hits = scan(push, True)
    check(len(hits) == 1, f'picture push-and-call matches once ({len(hits)})')
    if len(hits) == 1 and known:
        call = hits[0] + 15
        loader = call + 5 + struct.unpack('<i', read(call + 1))[0]
        check((call, loader) == (0x648D0D, 0x62B4D0), f'known build: call {call:#x}, loader {loader:#x}')

# loading screen text: three calls to one function, the "HINT:" font, the screen-height getter
lit = re.search(r'static const char \*sites\[NLT\] = \{(.*?)\};', src, re.S)
sites = re.findall(r'"([0-9A-F? ]+)"', lit.group(1)) if lit else []
check(len(sites) == 3, f'loading screen: {len(sites)} call patterns read from the C source')
targets, calls = set(), []
for i, sp in enumerate(sites):
    h = scan(sp, True)
    check(len(h) == 1, f'loading screen call {i + 1} matches once ({len(h)})')
    if len(h) == 1:
        call = h[0] + 6; calls.append(call)
        targets.add(call + 5 + struct.unpack('<i', read(call + 1))[0])
check(len(targets) == 1, f'loading screen calls all reach one function {[hex(t) for t in targets]}')
m = re.search(r'sizeSig = "([0-9A-F? ]+)"', src)
h = scan(m.group(1), True) if m else []
check(len(h) == 1, f'screen-size pattern matches once ({len(h)})')
if len(h) == 1:
    geth = h[0] + 14 + struct.unpack('<i', read(h[0] + 10))[0]
    b = read(geth, 6)
    check(b[0] == 0xA1 and b[5] == 0xC3, f'height getter at {geth:#x} is mov eax,[..]; ret')
    if known:
        check((geth, struct.unpack('<I', b[1:5])[0]) == (0x647E10, 0x9255F8), f'known build: height getter {geth:#x}')
if known and len(calls) == 3:
    check(calls == [0x499D8C, 0x499DFA, 0x499EC1] and targets == {0x61BAB0}, f'known build: calls {[hex(c) for c in calls]} to {[hex(t) for t in targets]}')

print('all patterns check out' + (' (and match the known build)' if known else '') if not fails else f'{len(fails)} problem(s)')
sys.exit(1 if fails else 0)
