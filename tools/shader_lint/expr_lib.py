import re,sys
def build(path):
    src=open(path).read()
    # flatten the {...} res blocks into r = expr statements
    env={}
    out={}
    lines=src.split('\n')
    i=0
    blk=None
    def ev(expr):
        def rep(m):
            return env.get(m.group(0), m.group(0))
        return re.sub(r'\b(r\d+|xa|xb|xbu|prod|cc_|res)\b', rep, expr)
    for l in lines:
        s=l.strip()
        if s.startswith('//') or not s: continue
        m=re.match(r'\{?\s*uint xa = (.*?); uint xb = (.*?); uint xbu = (.*?);$',s)
        if m:
            env['xa']='('+ev(m.group(1))+')'; env['xb']='('+ev(m.group(2))+')'; env['xbu']=ev(m.group(3)); continue
        for part in [p.strip() for p in s.split(';') if p.strip()]:
            part=part.lstrip('{').strip().rstrip('}').strip()
            m=re.match(r'(?:uint\s+)?(r\d+|prod|cc_|res|gl_Position\[\d\])\s*(<<=|=)\s*(.*)$',part)
            if not m: continue
            lhs,op,rhs=m.groups()
            if op=='<<=': val='(('+env.get(lhs,lhs)+')<<'+rhs+')'
            else: val='('+ev(rhs)+')'
            if lhs.startswith('gl_Position'): out[lhs]=val
            else: env[lhs]=val
    return out

# ---- canonical form: commutative operands sorted (a*b == b*a exactly in IEEE, so are fma's first two arguments)
import re as _re, sys as _sys, threading as _th
_TOK = _re.compile(r'\s*(0x[0-9a-fA-F]+u?|\d+\.?\d*(?:e[-+]?\d+)?[uf]?|[A-Za-z_]\w*|<<|>>|[-+*/&|^~!(),\[\]?:<>=])')
def _tokens(s):
    out = []; i = 0
    while i < len(s):
        m = _TOK.match(s, i)
        if not m: i += 1; continue
        out.append(m.group(1)); i = m.end()
    return out
_PREC = {'|': 1, '^': 2, '&': 3, '<<': 5, '>>': 5, '+': 6, '-': 6, '*': 7, '/': 7}
_COMM = {'+', '*', '&', '|', '^'}
def _parse(t):
    pos = [0]
    def peek(): return t[pos[0]] if pos[0] < len(t) else None
    def nxt(): pos[0] += 1; return t[pos[0] - 1]
    def primary():
        tok = nxt()
        if tok == '(':
            e = expr(0); nxt(); node = e
        elif tok in ('-', '!', '~'):
            node = ('u' + tok, primary())
        else:
            node = tok
            if peek() == '(':
                nxt(); args = []
                if peek() != ')':
                    args.append(expr(0))
                    while peek() == ',': nxt(); args.append(expr(0))
                nxt(); node = ('call', tok, args)
        while peek() == '[':
            nxt(); i = expr(0); nxt(); node = ('idx', node, i)
        return node
    def expr(minp):
        left = primary()
        while peek() in _PREC and _PREC[peek()] >= minp:
            op = nxt(); right = expr(_PREC[op] + 1); left = (op, left, right)
        if minp == 0 and peek() == '?':
            nxt(); a = expr(0); nxt(); b = expr(0); left = ('?', left, a, b)
        return left
    return expr(0)
def _canon(n):
    if isinstance(n, str): return n
    if n[0] == 'call':
        args = [_canon(a) for a in n[2]]
        if n[1] == 'fma' and len(args) == 3: args[:2] = sorted(args[:2])
        if n[1] in ('uintBitsToFloat', 'floatBitsToUint', 'as_u') and len(args) == 1:
            return args[0] if args[0].startswith(('uintBitsToFloat(', 'floatBitsToUint(')) and False else n[1] + '(' + args[0] + ')'
        return n[1] + '(' + ','.join(args) + ')'
    if n[0] == 'idx': return _canon(n[1]) + '[' + _canon(n[2]) + ']'
    if n[0] == '?': return '(' + ','.join(_canon(x) for x in n[1:]) + ')?'
    if n[0].startswith('u'): return n[0][1:] + _canon(n[1])
    a, b = _canon(n[1]), _canon(n[2])
    if n[0] in _COMM and b < a: a, b = b, a
    return '(' + a + n[0] + b + ')'
def canonical(s):
    res = [None]
    def run():
        _sys.setrecursionlimit(1000000); res[0] = _canon(_parse(_tokens(s)))
    _th.stack_size(512 * 1024 * 1024); th = _th.Thread(target=run); th.start(); th.join()
    return res[0]
