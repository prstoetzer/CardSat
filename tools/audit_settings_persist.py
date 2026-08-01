#!/usr/bin/env python3
"""
audit_settings_persist.py -- a setting the operator can change must survive a reboot.

THE PROBLEM
  Settings::save() and Settings::load() are hand-written lists of JSON keys. A field
  added to the struct but forgotten in either list produces a setting that works
  perfectly until the next power cycle and then silently reverts. There is no error,
  no log line, and nothing on screen -- the operator's most likely conclusion is
  that they never set it.

  This is the same failure shape as a load-time clamp written against a stale
  enumerator (see audit_settings_clamps.py), and it has now happened twice:
    * `rotMagCorrect` -- the rotator's bearing reference (true vs magnetic). It was
      a live Settings row that called save(), and the field appeared NOWHERE in
      settings.cpp. Every reboot silently reverted the rotator to true bearings,
      mispointing it by the local magnetic declination.

THE RULE
  Every field of `struct Settings` must be BOTH written in save() and read in
  load(). Deliberate exceptions go in EXEMPT below with a reason -- runtime-only
  scratch state is a reason; "it seemed unimportant" is not, because importance is
  exactly what is being assumed when a setting quietly resets.

METHOD
  Parse the field names out of `struct Settings`, then check each one appears on the
  left of an assignment in load() and inside save(). Both halves are checked
  separately: a field that is saved but never loaded is just as broken as one that
  is never saved, and reads the same way to the operator.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
H = os.path.join(ROOT, 'src', 'settings.h')
C = os.path.join(ROOT, 'src', 'settings.cpp')

# field -> why it is deliberately not persisted
EXEMPT = {
    'cfgFileMissing':
        'runtime-only: set by load() itself to record whether a config file existed '
        'at boot (so first-run defaults get written back once). Persisting it would '
        'be meaningless -- by definition the file exists once it has been saved.',
}

TYPES = r'(?:uint\d+_t|int\d*|bool|char|float|double|freq_t|time_t|size_t)'


def struct_fields(text):
    body = re.search(r'struct Settings\s*\{(.*?)\n\};', text, re.S)
    if not body:
        print('FAIL: could not find struct Settings')
        sys.exit(1)
    src = re.sub(r'//[^\n]*', '', body.group(1))
    src = re.sub(r'/\*.*?\*/', '', src, flags=re.S)
    out = []
    for m in re.finditer(TYPES + r'\s+(\w+)\s*(?:\[[^\]]*\])?\s*(?:\[[^\]]*\])?\s*[=;]', src):
        out.append(m.group(1))
    return out


def section(text, sig):
    """Body of a Settings:: member function, by signature fragment."""
    i = text.find(sig)
    if i < 0:
        return ''
    j = text.index('{', i)
    depth, k = 0, j
    while k < len(text):
        if text[k] == '{':
            depth += 1
        elif text[k] == '}':
            depth -= 1
            if depth == 0:
                break
        k += 1
    return text[j:k]


def main():
    h = open(H, encoding='utf-8', errors='replace').read()
    c = open(C, encoding='utf-8', errors='replace').read()
    fields = struct_fields(h)

    load_body = section(c, 'Settings::load')
    save_body = section(c, 'Settings::save')
    if not load_body or not save_body:
        print('FAIL: could not locate Settings::load / Settings::save')
        sys.exit(1)

    missing_load, missing_save = [], []
    for f in fields:
        if f in EXEMPT:
            continue
        # load: the field is assigned, or filled via strncpy/memcpy/snprintf
        if not re.search(r'\b' + re.escape(f) + r'\b', load_body):
            missing_load.append(f)
        if not re.search(r'\b' + re.escape(f) + r'\b', save_body):
            missing_save.append(f)

    if missing_load or missing_save:
        print('SETTINGS PERSISTENCE: field(s) do not round-trip through storage')
        for f in sorted(set(missing_load) | set(missing_save)):
            where = []
            if f in missing_load:
                where.append('never read in load()')
            if f in missing_save:
                where.append('never written in save()')
            print(f'  Settings::{f} -- {" and ".join(where)}')
        print('    -> the setting will appear to work and then silently revert on the')
        print('       next boot. Add it to both halves, or list it in EXEMPT with a reason.')
        sys.exit(1)

    print(f'settings persistence OK ({len(fields)} fields round-trip, '
          f'{len(EXEMPT)} exempt)')


if __name__ == '__main__':
    main()
