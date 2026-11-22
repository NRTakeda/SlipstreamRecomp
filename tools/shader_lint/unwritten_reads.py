#!/usr/bin/env python3
"""unwritten_reads.py <dir>: registers and predicates a translated shader reads but never writes (the translator may have
dropped the instruction that wrote them), with the guest instructions that read them."""
import re, sys, glob, collections
ASSIGN = re.compile(r'(?<![\w.])(r\d+|p\d+)\s*=(?!=)')
NAME = re.compile(r'(?<![\w.])(r\d+|p\d+)\b')
byop = collections.Counter(); ex = {}; nsh = 0
for path in sorted(glob.glob(sys.argv[1] + '/*.glsl')):
    text = open(path).read()
    body = text[text.find('void main'):]
    written = set(); reads = []
    word = None
    for line in body.split('\n'):
        m = re.match(r'\s*// pc ([0-9a-f]+): ([0-9a-f]{16})', line)
        if m: word = m.group(2); continue
        code = line.split('//')[0]
        if re.match(r'\s*(uint|bool) (r|p)\d+ = (0u|false);', code): continue
        for stmt in code.split(';'):
            a = ASSIGN.search(stmt)
            if a: written.add(a.group(1))
            rhs = stmt[a.end():] if a else stmt
            for n in NAME.findall(rhs): reads.append((n, word))
    bad = [(n, w) for n, w in reads if n not in written and w]
    if bad: nsh += 1
    for n, w in bad:
        byop[w[:4]] += 1; ex.setdefault(w[:4], (path.split('/')[-1], n, w))
print(nsh, 'shaders read something never written')
for op, k in byop.most_common(20): print(op, k, ex[op])
