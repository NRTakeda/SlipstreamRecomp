#!/usr/bin/env python3
"""equal_depth_pairs.py <debug log> <glsl dir>: every depth-EQUAL draw of a debug frame against the depth-writing draw before it
with the same vertex data and count, and whether their vertex shaders compute gl_Position the same way
(structurally: the same expression of the same inputs)."""
import re, sys, glob, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import importlib.util
spec = importlib.util.spec_from_file_location('expr', os.path.dirname(os.path.abspath(__file__)) + '/expr_lib.py')
E = importlib.util.module_from_spec(spec); spec.loader.exec_module(E)
log, gdir = sys.argv[1], sys.argv[2]
draws = []; cur = None
for l in open(log):
    if re.match(r'\[draw\] [ea] ', l): cur = {'h': l, 'vb': None, 'fs': None}; draws.append(cur)
    elif cur is not None:
        m = re.match(r'\[draw\]   attr0 .*vbuf (\w+)', l)
        if m: cur['vb'] = m.group(1)
        m = re.search(r'fs hash ([0-9a-f]{16})', l)
        if m and not cur['fs']: cur['fs'] = m.group(1)
for d in draws:
    d['vs'] = re.search(r'vs=(\w+)', d['h']).group(1)
    m = re.search(r' ds (\w+)', d['h']); d['ds'] = m.group(1) if m else ''
    d['count'] = re.search(r'count (\d+)', d['h']).group(1)
gl = {os.path.basename(f).split('_')[0]: f for f in glob.glob(gdir + '/*_vs.glsl')}
cache = {}
def pos(vs):
    if vs not in cache: cache[vs] = {k: E.canonical(v) for k, v in E.build(gl[vs]).items()} if vs in gl else None
    return cache[vs]
seen = set(); same = diff = unk = 0
for i, m in enumerate(draws):
    if not (m['ds'].startswith('01') and m['ds'][-1] == '3'): continue  # (test on, EQUAL)
    pre = [p for p in draws[:i] if p['vb'] == m['vb'] and p['count'] == m['count'] and p['ds'][2:4] == '01' and p is not m]
    if not pre: continue
    p = pre[-1]; key = (p['vs'], m['vs'])
    if key in seen: continue
    seen.add(key)
    a, b = pos(p['vs']), pos(m['vs'])
    if a is None or b is None: unk += 1; continue
    if a == b: same += 1; continue
    diff += 1
    print('DIFF  colour draw', i + 1, 'fs', m['fs'], 'vs', m['vs'], '| depth draw vs', p['vs'], 'fs', p['fs'])
    for k in sorted(set(a) | set(b)):
        x, y = a.get(k, ''), b.get(k, '')
        if x != y:
            n = next((j for j, (q, r) in enumerate(zip(x, y)) if q != r), min(len(x), len(y)))
            print('   ', k, 'depth:', x[max(0, n - 80):n + 80]); print('   ', k, 'colour:', y[max(0, n - 80):n + 80]); break
print(f'{same} same, {diff} different, {unk} without source')
