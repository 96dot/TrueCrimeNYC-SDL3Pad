"""Builds test/harness.exe and runs it against build/TCNYCSDL3Pad.asi (built by build_check.py, which
.checks.json runs first; a plugin older than its source is refused). Checks the input side only:
the virtual controller is offered, takes 17 / 17 / 13 actions on foot / driving / menus (10 kept after
the menu's keyboard entries are handed back), the keyboard and mouse keep theirs, and rumble is set up
with 2 motors. Prompts, text and the quit screen need the real game. Needs the game's tcnyc.exe for its
action tables; harness.c reads them at fixed offsets of the original build. Without it: exit 2."""
import os, re, shutil, subprocess, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
test = os.path.join(root, 'test')
r = subprocess.run(['cmd', '/c', os.path.join(test, 'build_test.bat')], cwd=test, capture_output=True, text=True)
if r.returncode != 0:
    print('harness build failed:', (r.stdout + r.stderr)[-400:])
    sys.exit(1)
asi = os.path.join(root, 'build', 'TCNYCSDL3Pad.asi')
newest_src = max(os.path.getmtime(os.path.join(root, f)) for f in ('tcnyc_sdl3pad.c', 'sdl3_min.h'))
if not os.path.exists(asi) or os.path.getmtime(asi) < newest_src:
    print('FAIL: build/TCNYCSDL3Pad.asi is missing or older than the source (did the build fail?)')
    sys.exit(1)
shutil.copy(asi, os.path.join(test, 'TCNYCSDL3Pad.asi'))
r = subprocess.run([os.path.join(test, 'harness.exe'), os.path.join(test, 'TCNYCSDL3Pad.asi'), '1'],
                   cwd=test, capture_output=True, text=True, timeout=120)
out = r.stdout
if 'cannot open tcnyc.exe' in out:
    print('NOT RUN: harness could not open tcnyc.exe (path at the top of test/harness.c)')
    sys.exit(2)
problems = []
if 'force-feedback axes: 2' not in out:
    problems.append('rumble device did not report 2 force-feedback axes')
if 'CreateEffect: 00000000' not in out:
    problems.append('CreateEffect did not succeed')
pad = re.findall(r'SDL3 Controller \(TCNYCSDL3Pad\)\s+Build (\w+) Set (\w+)\s+actions mapped at Set: (\d+) \(after Set: (\d+)\)', out)
kb = re.findall(r'Keyboard\s+Build (\w+) Set (\w+)\s+actions mapped at Set: (\d+)', out)
mouse = re.findall(r'Mouse\s+Build (\w+) Set (\w+)\s+actions mapped at Set: (\d+)', out)
want = [('17', '17'), ('17', '17'), ('13', '10')]
if [(a, b) for _, _, a, b in pad] != want:
    problems.append(f'controller actions {[(a, b) for _, _, a, b in pad]} instead of {want}')
if len(kb) != 3 or any(int(n) < 20 for _, _, n in kb):
    problems.append(f'keyboard mappings look wrong: {kb}')
if len(mouse) != 3 or any(int(n) < 5 for _, _, n in mouse):
    problems.append(f'mouse mappings look wrong: {mouse}')
for p in problems:
    print('FAIL', p)
print('harness ok: controller 17/17/13, keyboard and mouse mapped, rumble set up' if not problems else f'{len(problems)} problem(s)')
sys.exit(1 if problems else 0)
