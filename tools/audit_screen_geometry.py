#!/usr/bin/env python3
"""Screen-geometry audit: does every draw function stay inside 240x135, below the
16 px header, and above the y=127 footer line -- and does anything overdraw?

Display invariants (from header()/footer() and the sprite):
  * canvas is 240x135; font size 1 = 6x8 px, size 2 = 12x16.
  * header() paints y 0..15;   content should start at y >= 16.
  * footer() prints at y=127;  content text must satisfy y + 8*size <= 127
    in any function that also calls footer() (else the descender row collides).
  * anything with y + 8*size > 135 or x >= 240 is clipped by the sprite.

What this tool can and cannot see. It is a STATIC analyzer over App::draw*()
bodies: it tracks setTextSize(), evaluates literal coordinate arithmetic, and
follows the two loop idioms the codebase uses for lists --
    for (int i = A; i < N; ++i) { ... y = B + i * P ... }
    int y = B; for (...) { ... y += P; ... }
-- computing the worst-row y when A/N/B/P are literals. Coordinates it cannot
evaluate (runtime state, clamped scrollers, computed columns) are counted as
SKIPPED, not silently passed; the per-function skip count is the honesty figure.
Guarded rows (an `if (y > LIMIT) break/continue/return` in the loop body, or a
`min(...)` bound on the trip count) are treated as clamped and not flagged.

Findings are labeled, not auto-failed: a full-bleed map that skips header() MAY
draw at y<16 legitimately. Every flag wants a human read of the cited line.
Exit code 1 only for the unarguable classes: literal text past the sprite edge.
"""
import re, sys, os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC  = os.path.join(ROOT, 'src', 'app.cpp')
src  = open(SRC, encoding='utf-8', errors='replace').read()
lines = src.split('\n')

W, H = 240, 135
HDR_BOTTOM = 16
FOOTER_Y = 127

# ---------------------------------------------------------------------------
# Slice App::draw*() bodies by brace matching.
# ---------------------------------------------------------------------------
def line_of(pos):
    return src.count('\n', 0, pos) + 1

funcs = []
for m in re.finditer(r'void App::(draw[A-Za-z0-9_]*)\s*\([^)]*\)\s*\{', src):
    name = m.group(1)
    b = m.end() - 1
    depth = 0; i = b
    while i < len(src):
        if src[i] == '{': depth += 1
        elif src[i] == '}':
            depth -= 1
            if depth == 0: break
        i += 1
    funcs.append((name, b + 1, i, line_of(m.start())))

# ---------------------------------------------------------------------------
# Tiny arithmetic evaluator for coordinate expressions.
# ---------------------------------------------------------------------------
SAFE = re.compile(r'^[\d\s+\-*/()xyirckn_]*$')

def try_eval(expr, env):
    expr = expr.strip()
    if not expr: return None
    # substitute known env vars (longest first so 'yy' beats 'y')
    for k in sorted(env, key=len, reverse=True):
        expr = re.sub(r'\b%s\b' % re.escape(k), str(env[k]), expr)
    if not re.match(r'^[\d\s+\-*/().]+$', expr): return None
    try:
        v = eval(expr, {"__builtins__": {}})
        return int(v)
    except Exception:
        return None

# ---------------------------------------------------------------------------
# Per-function walk. We iterate statements in order, maintaining:
#   size          -- current text size (1 unless set)
#   env           -- literal int locals (x/y/row pitch constants)
#   loop stack    -- (var, lo, hi_exclusive) for literal for-loops
# and recording draw ops with evaluated (or None) coordinates.
# ---------------------------------------------------------------------------
STMT = re.compile(r'[^;{}]*[;{}]', re.S)

class Op:
    __slots__ = ('kind','x','y','w','h','ln','txt','guarded','size')
    def __init__(self, kind, x, y, w, h, ln, txt, guarded, size):
        self.kind, self.x, self.y, self.w, self.h = kind, x, y, w, h
        self.ln, self.txt, self.guarded, self.size = ln, txt, guarded, size

def audit_function(name, b, e, def_ln):
    body = src[b:e]
    ops = []
    skipped = 0
    calls_header = bool(re.search(r'\bheader\s*\(', body))
    calls_footer = bool(re.search(r'\bfooter\s*\(', body))

    size = 1
    env = {}
    cursor = [None, None]
    loop_guard = 0   # inside a loop body containing a y-bound guard
    loop_depth = 0
    guard_stack = []

    pos = 0
    for stm in STMT.finditer(body):
        s = stm.group(0)
        ln = line_of(b + stm.start())

        # for-loop header: for (int i = A; i < N; ...)
        fm = re.search(r'for\s*\(\s*(?:int|uint8_t|uint16_t|size_t)?\s*'
                       r'([A-Za-z_]\w*)\s*=\s*([^;]+);\s*\1\s*(<|<=)\s*([^;]+);', s)
        if fm and s.rstrip().endswith('{'):
            var, lo_e, cmp_, hi_e = fm.groups()
            lo = try_eval(lo_e, env); hi = try_eval(hi_e, env)
            loop_depth += 1
            # the body may guard rows: peek ahead to loop close for a y-guard
            gb = body.find('{', stm.start()); depth = 0; j = gb
            while j < len(body):
                if body[j] == '{': depth += 1
                elif body[j] == '}':
                    depth -= 1
                    if depth == 0: break
                j += 1
            loop_body = body[gb:j]
            guarded = bool(re.search(r'if\s*\(\s*\w*y\w*\s*[+\d\s]*[><]=?[^)]*\)\s*'
                                     r'(break|continue|return)', loop_body))
            guard_stack.append(guarded)
            if lo is not None and hi is not None:
                if cmp_ == '<=': hi += 1
                env[var] = hi - 1          # audit the worst (last) row
                env['__lo_' + var] = lo
            continue
        if s.strip() == '}' and loop_depth > 0:
            loop_depth -= 1
            if guard_stack: guard_stack.pop()
            # leave env values in place; harmless for later literals

        guarded = any(guard_stack)

        m = re.search(r'setTextSize\s*\(\s*(\d+)', s)
        if m: size = int(m.group(1))

        # literal int locals and += accumulators
        m = re.search(r'\bint\s+([A-Za-z_]\w*)\s*=\s*([^;,]+);', s)
        if m:
            v = try_eval(m.group(2), env)
            if v is not None: env[m.group(1)] = v
        m = re.search(r'\b([A-Za-z_]\w*)\s*\+=\s*([^;]+);', s)
        if m and m.group(1) in env:
            d = try_eval(m.group(2), env)
            if d is not None:
                env[m.group(1)] += d   # single step; loop accumulation handled by worst-row env
        m = re.search(r'\b([A-Za-z_]\w*)\s*=\s*([^=;][^;]*);', s)
        if m and '==' not in s and m.group(1) in env:
            v = try_eval(m.group(2), env)
            if v is not None: env[m.group(1)] = v

        m = re.search(r'setCursor\s*\(\s*([^,]+),\s*([^)]+)\)', s)
        if m:
            cursor = [try_eval(m.group(1), env), try_eval(m.group(2), env)]
            if cursor[1] is None: skipped += 1
            continue

        # print / printf right after a cursor set
        m = re.search(r'\bprintf?\s*\(\s*"((?:[^"\\]|\\.)*)"', s)
        if m and 'canvas' in s:
            lit = re.sub(r'%[-\d.]*[a-zA-Z]', '', m.group(1))  # literal min width
            w = 6 * size * len(lit)
            ops.append(Op('text', cursor[0], cursor[1], w, 8 * size, ln,
                          m.group(1)[:32], guarded, size))
            continue

        m = re.search(r'drawString\s*\(\s*("((?:[^"\\]|\\.)*)"|[^,]+),\s*([^,]+),\s*([^),]+)', s)
        if m:
            lit = m.group(2)
            w = 6 * size * len(lit) if lit is not None else 0
            x = try_eval(m.group(3), env); y = try_eval(m.group(4), env)
            if y is None: skipped += 1
            ops.append(Op('text', x, y, w, 8 * size, ln,
                          (lit or '<expr>')[:32], guarded, size))
            continue

        for prim in ('fillRect', 'drawRect'):
            m = re.search(prim + r'\s*\(\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),', s)
            if m:
                x = try_eval(m.group(1), env); y = try_eval(m.group(2), env)
                w = try_eval(m.group(3), env); h = try_eval(m.group(4), env)
                if y is None: skipped += 1
                ops.append(Op('rect', x, y, w or 0, h or 0, ln, prim, guarded, size))
                break

    findings = []
    for op in ops:
        if op.y is None: continue
        if op.guarded: continue
        y2 = op.y + (op.h or 0)
        if op.kind == 'text':
            if y2 > H:
                findings.append(('CLIP-BOTTOM', op))
            elif calls_footer and y2 > FOOTER_Y:
                # y2 == 128 is the established last-row idiom (baseline y=120): only
                # the descender row can touch the footer's cap row -- informational.
                findings.append(('FOOTER-OVERLAP' if y2 > FOOTER_Y + 1 else 'DESC-TOUCH', op))
            if calls_header and op.y < HDR_BOTTOM and op.y >= 0:
                findings.append(('UNDER-HEADER', op))
            if op.x is not None and op.txt and op.w and op.x + op.w > W:
                findings.append(('CLIP-RIGHT', op))
            if op.y < 0:
                findings.append(('NEG-Y', op))
        else:
            if op.y is not None and op.h and op.y + op.h > H + 1:
                findings.append(('RECT-OOB', op))
    # duplicate literal positions (possible overdraw of two different strings)
    seen = {}
    for op in ops:
        if op.kind != 'text' or op.x is None or op.y is None or not op.txt: continue
        key = (op.x, op.y)
        if key in seen and seen[key].txt != op.txt and not op.guarded:
            op.txt = f"{seen[key].txt!r}@{seen[key].ln} vs {op.txt!r}"
            findings.append(('SAME-XY', op))
        seen[key] = op
    return ops, skipped, findings, calls_header, calls_footer

total_ops = total_skip = 0
all_findings = []
hard_fail = 0
for name, b, e, def_ln in funcs:
    ops, skipped, findings, ch, cf = audit_function(name, b, e, def_ln)
    total_ops += len(ops); total_skip += skipped
    for kind, op in findings:
        all_findings.append((name, kind, op))
        if kind in ('CLIP-BOTTOM', 'CLIP-RIGHT', 'NEG-Y'):
            hard_fail += 1

print(f"screen-geometry audit: {len(funcs)} draw functions, "
      f"{total_ops} evaluated draw ops, {total_skip} skipped (runtime coords)")
if not all_findings:
    print("  no geometry findings")
else:
    for name, kind, op in all_findings:
        loc = f"app.cpp:{op.ln}"
        print(f"  [{kind:14s}] {name:24s} {loc:16s} "
              f"x={op.x} y={op.y} size={op.size} txt={op.txt!r}")
print(f"  hard failures (definite clip): {hard_fail}")
sys.exit(1 if hard_fail else 0)
