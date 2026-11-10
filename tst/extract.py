#!/usr/bin/env python3

# One-time extractor of the golden suite.
#
#   tst/extract.py <checkout of atmos-lang/atmos> [workers]
#
# Reads every `atm_test(src)` case in `tst/*.lua` of the reference
# implementation, with the output it expects, and runs it with this
# compiler:
#
#   - if the output is equivalent, the case goes to `tst/golden/`,
#     as `NNNN-name.atm` plus `NNNN-name.out`;
#   - otherwise it goes to `tst/dropped/`, as `NNNN-name.atm` plus
#     `NNNN-name.exp` (what the reference expects) and a line in
#     `tst/dropped/INDEX.md` with the reason.
#
# Equivalent means equal after the differences in the error traces of
# the two implementations are set aside.

import os
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

HERE  = os.path.dirname(os.path.abspath(__file__))
ATMOS = os.path.join(HERE, '..', 'atmos')

# in the order of `tst/all.lua`
FILES = [
    'exec', 'x', 'tasks', 'await', 'toggle', 'timers', 'throw',
    'streams', 'thread',
]

###############################################################################
# LUA TOKENS
###############################################################################

ESCS = {
    'n': '\n', 't': '\t', '\\': '\\', '"': '"', "'": "'", 'a': '\a',
    'b': '\b', 'f': '\f', 'r': '\r', 'v': '\v', '\n': '\n',
}

def long_open (s, i):
    # [[  [=[  [==[ ...: returns the level, or -1
    if s[i] != '[':
        return -1
    j = i + 1
    while j < len(s) and s[j] == '=':
        j += 1
    if j < len(s) and s[j] == '[':
        return j - i - 1
    return -1

def long_read (s, i, lvl):
    # returns (contents, position after the closing bracket)
    i += lvl + 2
    close = ']' + '='*lvl + ']'
    j = s.index(close, i)
    v = s[i:j]
    if v.startswith('\r\n'):
        v = v[2:]
    elif v.startswith('\n'):
        v = v[1:]
    return v, j + len(close)

def tokens (s):
    ret = []
    i = 0
    lin = 1
    while i < len(s):
        c = s[i]
        if c == '\n':
            lin += 1
            i += 1
        elif c.isspace():
            i += 1
        elif s.startswith('--', i):
            lvl = long_open(s, i+2) if i+2 < len(s) else -1
            if lvl >= 0:
                v, j = long_read(s, i+2, lvl)
                lin += s.count('\n', i, j)
                i = j
            else:
                while i < len(s) and s[i] != '\n':
                    i += 1
        elif long_open(s, i) >= 0:
            v, j = long_read(s, i, long_open(s, i))
            ret.append(('str', v, lin))
            lin += s.count('\n', i, j)
            i = j
        elif c in '"\'':
            j = i + 1
            v = []
            while s[j] != c:
                if s[j] == '\\':
                    j += 1
                    e = s[j]
                    if e in ESCS:
                        v.append(ESCS[e])
                        j += 1
                    elif e == 'x':
                        v.append(chr(int(s[j+1:j+3], 16)))
                        j += 3
                    elif e == 'z':
                        j += 1
                        while s[j].isspace():
                            j += 1
                    elif e.isdigit():
                        k = j
                        while k < j+3 and s[k].isdigit():
                            k += 1
                        v.append(chr(int(s[j:k])))
                        j = k
                    else:
                        raise Exception('escape : line %d' % lin)
                else:
                    v.append(s[j])
                    j += 1
            ret.append(('str', ''.join(v), lin))
            i = j + 1
        elif c.isalpha() or c == '_':
            j = i
            while j < len(s) and (s[j].isalnum() or s[j] == '_'):
                j += 1
            ret.append(('name', s[i:j], lin))
            i = j
        elif c.isdigit():
            j = i
            while j < len(s) and (s[j].isalnum() or s[j] == '.'):
                j += 1
            ret.append(('num', s[i:j], lin))
            i = j
        else:
            for op in ('...', '..', '==', '~=', '<=', '>=', '//', '::'):
                if s.startswith(op, i):
                    break
            else:
                op = c
            ret.append(('op', op, lin))
            i += len(op)
    return ret

###############################################################################
# CASES
###############################################################################

# trim of the reference: src/aux.lua
def trim (s):
    s = re.sub(r'^\s*', '', s)
    s = re.sub(r'\n\s*', '\n', s)
    return re.sub(r'\s*$', '', s)

class Parser:
    def __init__ (self, toks):
        self.ts = toks
        self.i  = 0

    def peek (self, k=0):
        if self.i + k < len(self.ts):
            return self.ts[self.i + k]
        return ('eof', '', 0)

    def isa (self, tag, val, k=0):
        t = self.peek(k)
        return t[0] == tag and t[1] == val

    def accept (self, tag, val):
        if self.isa(tag, val):
            self.i += 1
            return True
        return False

    # str | trim str | trim(strexpr) | (strexpr), joined by `..`
    def strexpr (self):
        v = self.strprim()
        while v is not None and self.accept('op', '..'):
            w = self.strprim()
            v = None if w is None else v + w
        return v

    def strprim (self):
        t = self.peek()
        if t[0] == 'str':
            self.i += 1
            return t[1]
        if self.accept('name', 'trim'):
            v = self.strprim()
            return None if v is None else trim(v)
        if self.accept('op', '('):
            v = self.strexpr()
            if not self.accept('op', ')'):
                return None
            return v
        return None

    # out | trim(out): returns whether trimmed, or None
    def out (self):
        if self.accept('name', 'out'):
            return False
        if self.isa('name','trim') and self.isa('op','(',1) and \
           self.isa('name','out',2) and self.isa('op',')',3):
            self.i += 4
            return True
        return None

def cases (path):
    p = Parser(tokens(open(path).read()))
    ret = []
    src  = None
    name = None
    cur  = None
    while p.peek()[0] != 'eof':
        t = p.peek()
        i0 = p.i
        if p.accept('name', 'src') and p.accept('op', '='):
            src = p.strexpr()
        elif t == ('name','print',t[2]) and p.isa('op','(',1) and \
             p.isa('str','Testing...',2) and p.isa('op',',',3):
            p.i += 4
            if p.accept('name', 'src'):
                name = src
            else:
                name = p.strexpr()
        elif t[1] == 'out' and p.isa('op','=',1) and p.isa('name','atm_test',2):
            p.i += 3
            cur = {
                'name': name or '?', 'src': src, 'lin': t[2],
                'exact': [], 'find': [], 'bad': [],
            }
            ret.append(cur)
        elif t[1] in ('assertx', 'warnx') and p.isa('op','(',1):
            p.i += 2
            tr = p.out()
            if tr is None or not p.accept('op', ','):
                p.i = i0 + 1
                continue
            exp = p.strexpr()
            if cur is not None:
                if exp is None or not p.isa('op', ')'):
                    cur['bad'].append('expected output not literal')
                elif t[1] == 'warnx':
                    cur['bad'].append('known failure in the reference')
                else:
                    cur['exact'].append((tr, exp))
        elif t[1] == 'assertfx' and p.isa('op','(',1) and p.isa('name','out',2):
            p.i += 4
            exp = p.strexpr()
            if cur is not None:
                if exp is None:
                    cur['bad'].append('expected output not literal')
                else:
                    cur['find'].append(exp)
        elif t[1] in ('assert', 'warn') and p.isa('op','(',1) and \
             p.isa('name','out',2) and p.isa('op','==',3):
            p.i += 4
            exp = p.strexpr()
            if cur is not None:
                if exp is None:
                    cur['bad'].append('expected output not literal')
                elif t[1] == 'warn':
                    cur['bad'].append('known failure in the reference')
                else:
                    cur['exact'].append((False, exp))
        else:
            p.i = i0 + 1
    return ret

###############################################################################
# COMPARISON
###############################################################################

# Error traces: the reference shows Lua positions and the frames of
# its scheduler, this compiler shows source positions only.
def norm (s):
    ret = []
    for l in s.split('\n'):
        # the indentation is lost when the expected output is trimmed
        x = l.lstrip(' ')
        if x.startswith('|  '):
            continue
        if x.startswith('v  '):
            l = re.sub(r'^v  \[string "anon\.atm"\]:(\d+) ', r'v  anon.atm:\1 ', x)
        ret.append(l)
    return '\n'.join(ret)

# Lua patterns in `assertfx`: only the simple ones are understood.
def lua_find (out, pat):
    if re.search(r'%[^%.\-\[\]()*+?^$a-z]', pat):
        return None
    cls = { 'd': r'\d', 's': r'\s', 'w': r'\w', 'a': '[A-Za-z]', 'x': '[0-9A-Fa-f]' }
    r = []
    i = 0
    while i < len(pat):
        c = pat[i]
        if c == '%':
            i += 1
            e = pat[i]
            r.append(cls[e] if e in cls else re.escape(e))
        elif c == '-':
            r.append('*?')
        elif c in '.*+?[]^$()':
            r.append(c)
        else:
            r.append(re.escape(c))
        i += 1
    try:
        return re.search(''.join(r), out, re.S) is not None
    except re.error:
        return None

def run (src, name='anon'):
    with tempfile.TemporaryDirectory() as d:
        open(os.path.join(d, name+'.atm'), 'w').write(src)
        env = dict(os.environ, ATMOS_ASAN='1', ATMOS_CFLAGS='-O0')
        try:
            r = subprocess.run([ATMOS, name+'.atm'], cwd=d, env=env,
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, timeout=20)
            return r.stdout.decode(errors='replace')
        except subprocess.TimeoutExpired:
            return None

# Returns None if equivalent, or the reason.
def verdict (c, out):
    if c['src'] is None:
        return 'source not literal'
    if c['bad']:
        return c['bad'][0]
    if not c['exact'] and not c['find']:
        return 'no expected output'
    if out is None:
        return 'timeout'
    if 'AddressSanitizer' in out or 'LeakSanitizer' in out:
        return 'sanitizer'
    for tr, exp in c['exact']:
        cur = trim(out) if tr else out
        if cur != exp and norm(cur) != norm(exp):
            return 'output differs'
    for pat in c['find']:
        ok = lua_find(out, pat)
        if ok is None:
            return 'pattern not understood'
        if not ok:
            return 'output differs'
    return None

###############################################################################

def slug (s):
    s = re.sub(r'[^a-z0-9]+', '-', s.lower()).strip('-')
    return s[:40].strip('-') or 'case'

def main ():
    ref = sys.argv[1]
    workers = int(sys.argv[2]) if len(sys.argv) > 2 else 4
    all = []
    for f in FILES:
        for c in cases(os.path.join(ref, 'tst', f+'.lua')):
            c['file'] = f
            all.append(c)

    def one (c):
        c['out'] = None if c['src'] is None else run(c['src'])
        c['why'] = verdict(c, c['out'])
    with ThreadPoolExecutor(workers) as ex:
        list(ex.map(one, all))

    golden  = os.path.join(HERE, 'golden')
    dropped = os.path.join(HERE, 'dropped')
    os.makedirs(golden,  exist_ok=True)
    os.makedirs(dropped, exist_ok=True)
    index = []
    n = { None: 0 }
    kept = set(f[:4] for f in os.listdir(golden) if f.endswith('.atm'))
    for i, c in enumerate(all):
        name = '%04d-%s' % (i+1, slug(c['name']))
        # a case adapted by hand stays as it is
        if name[:4] in kept and c['why'] is not None:
            n['adapted by hand'] = n.get('adapted by hand', 0) + 1
            continue
        src = c['src'] or ''
        if c['why'] is None:
            open(os.path.join(golden, name+'.atm'), 'w').write(src)
            # the trace shows the name of the file
            out = run(src, name)
            open(os.path.join(golden, name+'.out'), 'w').write(out.rstrip('\n') + '\n')
        else:
            open(os.path.join(dropped, name+'.atm'), 'w').write(src)
            exp = [e for _,e in c['exact']] + c['find']
            open(os.path.join(dropped, name+'.exp'), 'w').write('\n'.join(exp))
            index.append('| `%s` | %s.lua:%d | %s |' %
                (name, c['file'], c['lin'], c['why']))
        n[c['why']] = n.get(c['why'], 0) + 1

    with open(os.path.join(dropped, 'INDEX.md'), 'w') as f:
        f.write('# Dropped cases\n\n')
        f.write('Cases of the reference suite that this compiler does not\n')
        f.write('reproduce, with the reason found by `tst/extract.py`.\n\n')
        f.write('| Case | Origin | Reason |\n|---|---|---|\n')
        f.write('\n'.join(index) + '\n')

    print('cases   %d' % len(all))
    print('golden  %d' % n[None])
    for k, v in sorted(n.items(), key=lambda kv: -kv[1]):
        if k is not None:
            print('dropped %4d  %s' % (v, k))

if __name__ == '__main__':
    main()
