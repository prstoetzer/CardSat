#!/usr/bin/env python3
"""
audit_status_width.py -- a status message the operator cannot finish reading is
worse than no message.

THE PROBLEM
  The transient status bar is drawn with `canvas.setCursor(2, 115)` at font size 1
  (6 px per character) on a 240 px display, so exactly (240 - 2) // 6 = 39
  characters are visible. Nothing truncates: `canvas.print(status)` writes straight
  into the sprite and everything past column 39 is silently clipped.

  That is at its worst for REFUSALS, which is most of what the dual-rig engage path
  emits -- and in those the actionable half is at the END:

      "Dual USB + USB rotator: move rotator off USB"   (44) -> "...off U" lost
      "Dual USB: legs share one adapter - renominate"  (45) -> "...renomi" lost
      "<radio> is receive-only - not an uplink"        (43) -> "...uplin" lost

  All three shipped. The operator saw a truncated sentence and no way to act on it.

METHOD (deliberately sound rather than complete)
  For each setStatus(...) call, sum the lengths of the STRING LITERALS in its first
  argument. The rendered message is at least that long, whatever the runtime values
  interpolate to, so flagging `sum > 39` cannot produce a false positive. Calls
  built entirely from runtime values are invisible to this check -- that is the
  "not complete" half, and it is why the budget below is stated in the failure
  message so a human can finish the job.

  A concatenation like `String(radio.name) + " is RX-only: not an uplink"` counts
  only the literal (26). The gate therefore will not catch a long runtime value on
  its own; where a call interpolates something unbounded, keep the literal short
  enough to leave room and say so in a comment.

  TERNARIES are branches, not concatenation: `cond ? "aaa" : "bbb"` renders ONE of
  the two, so their lengths are MAXed rather than summed. Summing them was the
  first version of this gate and it produced immediate false positives on real
  code. String literals are masked out before the split so that a ':' inside a
  message ("Dual USB: ...") is not mistaken for a ternary.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'src', 'app.cpp')

# canvas.setCursor(2, 115) at 6 px/char on a 240 px sprite.
STATUS_X = 2
BUDGET = (240 - STATUS_X) // 6

# Literal budget for calls that also interpolate a runtime value: leave room for it.
# 12 characters is sized for the common case, a radio or file name.
INTERPOLATED_HEADROOM = 12

# Messages whose interpolated value is provably SHORT, so the flat headroom above
# over-charges them. Keyed by a distinctive literal; each needs a reason.
ALLOW = {
    'Extras full (':
        'interpolates CTX_MAX, a 1-3 digit compile-time constant, not a name',
}


def literals_in(expr):
    """Every double-quoted literal in the expression, un-escaped, as a list."""
    out = []
    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', expr):
        s = m.group(1)
        s = s.replace('\\"', '"').replace('\\\\', '\\').replace('\\n', '\n')
        out.append(s)
    return out


def widest_branch(expr, headroom):
    """Widest a single rendered branch of this expression can be.

    Literals are masked first so a ':' inside a message cannot look like a ternary,
    then the masked text is split on '?' and ':' (ignoring '::'). Within a branch,
    concatenated literals SUM; across branches we take the MAX, because only one
    branch ever renders.

    `headroom` is added only to branches that actually interpolate a runtime value.
    Judging that per branch matters: in
        cond ? (String("Saved ") + path) : "Export failed (no filesystem?)"
    the runtime value is on the SHORT branch, and charging the long branch for it
    was a false positive in the first version of this gate.
    """
    lits = []

    def stash(m):
        lits.append(m.group(0))
        return '\x00%d\x00' % (len(lits) - 1)

    masked = re.sub(r'"(?:[^"\\]|\\.)*"', stash, expr)
    masked = masked.replace('::', '\x01')          # scope operator is not a ternary
    best = 0
    for part in re.split(r'[?:]', masked):
        total = 0
        for idx in re.findall(r'\x00(\d+)\x00', part):
            total += sum(len(t) for t in literals_in(lits[int(idx)]))
        if total == 0:
            continue
        # Does THIS branch splice in a runtime value? (an identifier or call next
        # to the literal, rather than only literal text and operators)
        stripped = re.sub(r'\x00\d+\x00', '', part)
        if re.search(r'[A-Za-z_]\w*', stripped.replace('String', '')):
            total += headroom
        elif 'String' in stripped and '+' in stripped:
            total += headroom
        best = max(best, total)
    return best


def first_arg(src, open_paren):
    """Text of the first argument of a call whose '(' is at open_paren."""
    depth, i, start = 0, open_paren, open_paren + 1
    instr = False
    while i < len(src):
        c = src[i]
        if instr:
            if c == '\\':
                i += 2
                continue
            if c == '"':
                instr = False
        elif c == '"':
            instr = True
        elif c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return src[start:i]
        elif c == ',' and depth == 1:
            return src[start:i]
        i += 1
    return ''


def main():
    src = open(SRC, encoding='utf-8', errors='replace').read()
    findings = []
    checked = 0
    for m in re.finditer(r'\bsetStatus\s*\(', src):
        arg = first_arg(src, m.end() - 1)
        lits = literals_in(arg)
        if not lits:
            continue
        checked += 1
        if any(k in t for t in lits for k in ALLOW):
            continue
        total = widest_branch(arg, INTERPOLATED_HEADROOM)
        if total > BUDGET:
            line = src[:m.start()].count('\n') + 1
            findings.append((line, total, BUDGET, False,
                             ' + '.join(repr(t) for t in lits)))

    # ---- FOOTERS, same geometry ------------------------------------------------
    # footer() draws at setCursor(2, 127), size 1, on the same 240 px sprite, so it
    # has the SAME 39-column budget and the same silent clipping. It was not covered
    # here, and two footers were over: one added in 0.9.70 (49 cols) and one
    # pre-existing at 40, which had been quietly losing the last character of "print".
    for m in re.finditer(r'\bfooter\s*\(', src):
        arg = first_arg(src, m.end() - 1)
        lits = literals_in(arg)
        if not lits:
            continue
        checked += 1
        if any(k in t for t in lits for k in ALLOW):
            continue
        total = widest_branch(arg, INTERPOLATED_HEADROOM)
        if total > BUDGET:
            line = src[:m.start()].count('\n') + 1
            findings.append((line, total, BUDGET, True,
                             ' + '.join(repr(t) for t in lits)))

    if findings:
        print(f'STATUS/FOOTER WIDTH: text exceeds the {BUDGET}-column row')
        for line, total, limit, is_footer, text in sorted(findings):
            what = 'footer' if is_footer else 'status'
            print(f'  app.cpp:{line}: {what} widest branch ~{total} cols, budget {limit}')
            print(f'    {text}')
            print(f'    -> the row is {BUDGET} columns and nothing truncates;')
            print(f'       everything past it is clipped, including the actionable end.')
        sys.exit(1)

    print(f'status/footer widths OK ({checked} strings checked against '
          f'{BUDGET} columns)')


if __name__ == '__main__':
    main()
