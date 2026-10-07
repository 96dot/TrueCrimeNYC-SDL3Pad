"""Lists kept in step by hand, checked against the C source:
- every button name the plugin accepts (g_btnNames) is listed in README.md and in the ini's comment;
- every action's default button (the ActDef tables) is what the ini sets.
The README's and the Nexus page's control tables are prose and are not checked."""
import os, re, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
rd = lambda f: open(os.path.join(root, f), encoding='utf-8').read()
src, readme, ini = rd('tcnyc_sdl3pad.c'), rd('README.md'), rd('TCNYCSDL3Pad.ini')
fails = []
def check(ok, what):
    print(('ok    ' if ok else 'FAIL  ') + what)
    if not ok: fails.append(what)

table = src[src.index('g_btnNames[] = {'):src.index('};', src.index('g_btnNames[] = {'))]
names = re.findall(r'\{"([A-Z0-9_]+)",', table)
check(len(names) > 30, f'{len(names)} button names read from the C source')
sec = readme[readme.index('sections assign each action to a button'):readme.index('## Known issues')]
listed = set()   # every word inside the README's code spans, with PADDLE1-PADDLE4 spelled out
for span in re.findall(r'`([^`]*)`', sec): listed.update(span.split())
if {'PADDLE1', 'PADDLE4'} <= listed: listed.update({'PADDLE2', 'PADDLE3'})
ini_head = ini[:ini.index('[Settings]')]
for n in names:
    check(n in listed, f'README lists {n}')
    check(re.search(r'\b' + n + r'\b', ini_head) is not None, f'ini comment lists {n}')
check(re.search(r'\bNONE\b', ini_head) and 'NONE' in listed, 'NONE listed in both')

for sect in ('g_actFoot', 'g_actDrive', 'g_actMenu'):
    t = src[src.index(sect + '[] = {'):src.index('};', src.index(sect + '[] = {'))]
    defaults = re.findall(r'\{0x[0-9A-F]{2}, "(\w+)", "([A-Z0-9_]*)"\}', t)
    name = {'g_actFoot': 'OnFoot', 'g_actDrive': 'Driving', 'g_actMenu': 'Menus'}[sect]
    body = ini[ini.index('[' + name + ']') + len(name) + 2:]
    body = body[:body.index('\n[')] if '\n[' in body else body
    kv = dict(re.findall(r'^(\w+)=([A-Z0-9_]*)\s*$', body, re.M))
    for key, d in defaults:
        check(kv.get(key) == d, f'[{name}] {key}: code default "{d}", ini "{kv.get(key)}"')
    check(set(kv) == {k for k, _ in defaults}, f'[{name}] has exactly the actions the code knows')

print('names and defaults in step' if not fails else f'{len(fails)} problem(s)')
sys.exit(1 if fails else 0)
