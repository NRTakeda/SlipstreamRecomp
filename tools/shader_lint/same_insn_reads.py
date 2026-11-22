#!/usr/bin/env python3
"""same_insn_reads.py <dir>: translated shaders (SAVAGEWIND_XLATE_DUMP) whose emitted code, within one guest instruction
(a "// pc X: word" block), reads a register another line of that same instruction already wrote."""
import re, sys, glob, collections
ASSIGN = re.compile(r'(?<![\w.])r(\d+)\s*=(?!=)')
REG = re.compile(r'(?<![\w.])r(\d+)\b')
hits = collections.defaultdict(list)
nfiles = 0
for path in sorted(glob.glob(sys.argv[1] + '/*.glsl')):
    nfiles += 1
    pc = word = None; written = {}
    for ln, line in enumerate(open(path), 1):
        m = re.match(r'\s*// pc ([0-9a-f]+): ([0-9a-f]{16})', line)
        if m: pc, word = m.groups(); written = {}; continue
        if pc is None: continue
        code = line.split('//')[0]
        for stmt in code.split(';'):
            a = ASSIGN.search(stmt)
            lhs = a.group(1) if a else None
            rhs = stmt[a.end():] if a else stmt
            for r in REG.findall(rhs):
                if r in written and r != lhs:
                    hits[word[:4]].append((path.split('/')[-1], pc, word, r, written[r], stmt.strip()[:110]))
            if lhs is not None: written[lhs] = ln
print(nfiles, 'shaders')
for op, l in sorted(hits.items(), key=lambda x: -len(x[1])):
    files = len({h[0] for h in l})
    print(f'opcode {op}..: {len(l)} hits in {files} shaders')
    for h in l[:3]: print('   ', h)
