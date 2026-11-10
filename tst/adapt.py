# Adaptations of the reference cases to this language, applied by
# `tst/extract.py`.
#
#   idx     literal indexes move to base 0: `@1` and `@(1)` become
#           `@0` and `@(0)`
#   ops     `print(a OP b)` becomes a call, for the operators that
#           became functions
#   sub     textual replacements, in order
#   accept  the output differs from the reference by design, for the
#           given reason
#
# A case that is only rewritten must still give the output that the
# reference expects.

BASE0 = 'indexes start at 0'
MSG   = 'the error has another message'
DEAD  = 'a terminated task needs no owner'
TYPE  = 'tasks and pools have their own types'

# vector push and pop written with explicit positions
PUSH = [('t@(#t) = nil', 't@(#t-1) = nil'), ('#t+1', '#t')]

IDX = { 'idx': True }

ADAPT = {
    '0008': { 'ops': True },
    '0009': { 'ops': True },
    '0010': { 'sub': [('(2 ** 3) ** 2', 'math.pow(math.pow(2,3), 2)')] },
    '0021': { 'accept': 'an expression may be a statement' },
    '0039': { 'accept': MSG },
    '0043': { 'accept': 'val is immutable, also in a multiple declaration' },
    '0046': { 'accept': BASE0 },
    '0047': { 'accept': MSG },
    '0049': { 'sub': [('(([1])@1)@1', '(([0])@0)@0')] },
    '0050': { 'sub': [('(([1])@1)', '(([0])@0)')] },
    '0055': { 'sub': PUSH + [('t@1', 't@0')] },
    '0056': { 'sub': PUSH },
    '0057': { 'sub': PUSH },
    '0058': { 'sub': PUSH + [('t@(2)', 't@(1)')] },
    '0060': { 'sub': PUSH },
    '0061': { 'sub': PUSH + [('t@1', 't@0'), ('t@(2)', 't@(1)'), ('t@3', 't@2')] },
    '0063': { 'sub': [('t@(#t)', 't@(#t+1)')], 'accept': BASE0 },
    '0105': { 'accept': BASE0 },
    '0106': { 'accept': BASE0 },
    '0112': { 'accept': BASE0 },
    '0113': { 'accept': BASE0 },
    '0121': { 'accept': 'break is lexical' },
    '0131': { 'accept': MSG },
    '0164': { 'accept': TYPE },
    '0171': { 'sub': [("'table'", "'xtask'")] },
    '0172': { 'sub': [("'table'", "'xtask'")] },
    '0202': { 'accept': MSG },
    '0203': { 'accept': MSG },
    '0216': { 'accept': MSG },
    '0223': { 'ops': True },
    '0224': { 'ops': True },
    '0225': { 'ops': True },
    '0226': { 'ops': True },
    '0227': { 'ops': True },
    '0228': { 'ops': True },
    '0229': { 'ops': True },
    '0230': { 'ops': True },
    '0231': { 'ops': True },
    '0232': { 'ops': True },
    '0235': { 'sub': PUSH },
    '0241': { 'ops': True, 'accept': '?? compares tables field by field' },
    '0242': { 'ops': True },
    '0244': { 'sub': [('(\\===)(', 'X.eq(')] },
    '0255': IDX,
    '0262': { 'accept': MSG },
    '0277': { 'sub': [('print(co)', 'print(type(co))')], 'accept': DEAD },
    '0281': { 'accept': DEAD },
    '0294': IDX,
    '0295': IDX,
    '0297': IDX,
    '0299': IDX,
    '0300': IDX,
    '0301': IDX,
    '0303': IDX,
    '0304': { 'sub': [('e@(1)', 'e@(0)'), ('it@1', 'it@0')],
              'accept': 'catch does not stop a return' },
    '0305': IDX,
    '0306': IDX,
    '0307': IDX,
    '0308': IDX,
    '0309': IDX,
    '0310': IDX,
    '0313': IDX,
    '0314': IDX,
    '0315': IDX,
    '0316': IDX,
    '0317': IDX,
    '0318': IDX,
    '0319': IDX,
    '0321': { 'sub': [("type(e)!='table'", "type(e)=='string'")] },
    '0323': IDX,
    '0327': IDX,
    '0328': IDX,
    '0338': { 'accept': DEAD },
    '0348': { 'sub': [("'table'", "'xtask'")] },
    '0356': IDX,
    '0425': { 'accept': TYPE },
    '0438': { 'sub': [('atmos.gc()', '')],
              'accept': 'a terminated task leaves its pool at once' },
    '0444': IDX,
    '0475': { 'accept': 'await combinators follow the precedence of C' },
    '0489': { 'accept': BASE0 },
}

###############################################################################

import re

OPS = {
    '**':  'math.pow(%s, %s)',
    '===': 'X.eq(%s, %s)',
    '=!=': 'X.neq(%s, %s)',
    '=>=': 'X.gte(%s, %s)',
    '=<=': 'X.lte(%s, %s)',
    '?>':  'X.xin(%s, %s)',
    '!>':  'X.nin(%s, %s)',
}

def ops (src):
    def one (m):
        a, op, b = m.group(1), m.group(2), m.group(3)
        return 'print(' + (OPS[op] % (a.strip(), b.strip())) + ')'
    pat = r'print\((.*?) (\*\*|===|=!=|=>=|=<=|\?>|!>) (.*)\)'
    return '\n'.join(re.sub(pat, one, l, count=1) for l in src.split('\n'))

def idx (src):
    src = re.sub(r'@\((\d+)\)', lambda m: '@(%d)' % (int(m.group(1))-1), src)
    return re.sub(r'@(\d+)', lambda m: '@%d' % (int(m.group(1))-1), src)

# Returns the adapted source, how it was rewritten (or None), and the
# reason to accept another output (or None).
def adapt (num, src):
    a = ADAPT.get(num)
    if a is None:
        return src, None, None
    how = []
    if a.get('idx'):
        src = idx(src)
        how.append(BASE0)
    if a.get('ops'):
        src = ops(src)
        how.append('operators that became functions')
    for old, new in a.get('sub', []):
        src = src.replace(old, new)
    if a.get('sub') and not how:
        how.append('rewritten')
    return src, ', '.join(how) or None, a.get('accept')
