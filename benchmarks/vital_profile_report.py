# Makes vital_bench --profile samples a report: the built-in's samples by function
# (outermost, innermost inlined, inclusive) and by source line, with addr2line
# (MinGW's 64-bit binutils on the PATH) on a vital_bench built with -g.
#
#     python vital_profile_report.py build/bin/vital_bench.exe samples.txt [image base, default 0x140000000]

import subprocess, sys, collections, re, os
exe, samples = sys.argv[1], sys.argv[2]
base = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x140000000
offs = collections.Counter()
total = collections.Counter()
for line in open(samples):
    label, mod, off = line.split()
    total[label] += 1
    if label == 'builtin' and mod.lower() == os.path.basename(exe).lower():
        offs[int(off, 16)] += 1
addrs = list(offs)
out = subprocess.run(['addr2line', '-e', exe, '-f', '-i', '-C', '-a'] + [hex(base + a) for a in addrs],
                     capture_output=True, text=True).stdout.splitlines()
# parse: address line, then pairs (func, file:line) ...
frames = {}
cur = None
i = 0
while i < len(out):
    l = out[i]
    if l.startswith('0x') and re.fullmatch(r'0x[0-9a-f]+', l):
        cur = int(l, 16) - base; frames[cur] = []; i += 1; continue
    frames[cur].append((l, out[i + 1] if i + 1 < len(out) else '')); i += 2
def short(f):
    f = re.sub(r'\(.*', '', f)
    return f
n = total['builtin']
# The symbol table's function for each address (nm): right where the debug info
# isn't (C runtime functions without it, such as MinGW's libm, get misattributed
# to the nearest code that has it).
syms = []
for line in subprocess.run(['nm', '-C', '--defined-only', exe], capture_output=True, text=True,
                           errors='replace').stdout.splitlines():
    parts = line.split(' ', 2)
    if len(parts) == 3 and parts[1] in 'Tt' and not parts[2].startswith('.'):
        try:
            syms.append((int(parts[0], 16) - base, parts[2]))
        except ValueError:
            pass
syms.sort()
import bisect
keys = [v for v, _ in syms]
bysym = collections.Counter()
for a, c in offs.items():
    i = bisect.bisect_right(keys, a) - 1
    bysym[short(syms[i][1]) if i >= 0 else '??'] += c
inner = collections.Counter(); outer = collections.Counter(); lines = collections.Counter(); incl = collections.Counter()
for a, c in offs.items():
    fr = frames.get(a, [('??', '??')])
    inner[short(fr[0][0])] += c
    outer[short(fr[-1][0])] += c
    loc = fr[0][1].split(' (')[0]
    loc = re.sub(r'.*[\/](src|third_party)[\/]', '', loc)
    lines[short(fr[0][0]) + '  @ ' + loc] += c
    for f in set(short(x[0]) for x in fr):
        incl[f] += c
def show(title, ctr, k=25):
    print(f'\n== {title} (% of built-in samples, {n} total)')
    for f, c in ctr.most_common(k):
        print(f'{100*c/n:6.2f}%  {f[:150]}')
print({k: v for k, v in total.items()})
show('function by the symbol table (nm: always right, never inlined detail)', bysym, 30)
show('outermost real function (where time is spent, by processor)', outer, 30)
show('innermost (inlined) function', inner, 30)
show('inclusive (anywhere in the inline chain)', incl, 40)
show('hottest source lines', lines, 30)
