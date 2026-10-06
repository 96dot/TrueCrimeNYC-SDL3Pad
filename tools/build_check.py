"""Builds the plugin with build.bat and fails on any compiler or linker warning."""
import os, re, subprocess, sys

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
r = subprocess.run(['cmd', '/c', os.path.join(root, 'build.bat')], cwd=root, capture_output=True, text=True)
out = r.stdout + r.stderr
warnings = [l for l in out.splitlines() if re.search(r'\b(warning|error) [A-Z]+\d+', l)]
for l in warnings:
    print(l.strip())
if r.returncode != 0:
    print(f'build failed (exit {r.returncode})')
    sys.exit(1)
if warnings:
    print(f'{len(warnings)} warning(s)')
    sys.exit(1)
print('built with no warnings')
