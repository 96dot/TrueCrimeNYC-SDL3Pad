"""Builds the release zip, dist/TCNYCSDL3Pad-v<version>.zip, extracted into the game folder:
  SDL3.dll                               from the game folder one level up (the tested copy)
  scripts/TCNYCSDL3Pad.asi               build/TCNYCSDL3Pad.asi (or the path given as argument, e.g. the one that ran in the game)
  scripts/TCNYCSDL3Pad.ini
  scripts/TCNYCSDL3Pad/                  README, CHANGELOG, LICENSE, docs/, licenses/ with the repository's layout, so the
                                         README's links keep working
Every file is read back from the zip and compared by hash."""
import hashlib, os, re, sys, zipfile

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
game = os.path.dirname(root)
ver = re.search(r'#define VERSION "([\d.]+)"', open(os.path.join(root, 'tcnyc_sdl3pad.c'), encoding='utf-8').read()).group(1)
asi = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 'build', 'TCNYCSDL3Pad.asi')
files = [
    ('SDL3.dll', os.path.join(game, 'SDL3.dll')),
    ('scripts/TCNYCSDL3Pad.asi', asi),
    ('scripts/TCNYCSDL3Pad.ini', os.path.join(root, 'TCNYCSDL3Pad.ini')),
] + [('scripts/TCNYCSDL3Pad/' + f, os.path.join(root, f)) for f in
     ('README.md', 'CHANGELOG.md', 'LICENSE', 'docs/HOW-IT-WAS-FIXED.md', 'licenses/SDL3-LICENSE.txt')]
for _, src in files:
    if not os.path.exists(src): sys.exit(f'missing: {src}')
os.makedirs(os.path.join(root, 'dist'), exist_ok=True)
out = os.path.join(root, 'dist', f'TCNYCSDL3Pad-v{ver}.zip')
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for arc, src in files: z.write(src, arc)
md5 = lambda b: hashlib.md5(b).hexdigest()
with zipfile.ZipFile(out) as z:
    assert z.testzip() is None
    for arc, src in files:
        a, b = md5(z.read(arc)), md5(open(src, 'rb').read())
        print(('ok  ' if a == b else 'BAD ') + a + '  ' + arc)
        if a != b: sys.exit(1)
print(out, os.path.getsize(out), 'bytes, md5', md5(open(out, 'rb').read()))
