#!/usr/bin/env python3
"""
check_app_fits.py -- does the built binary actually fit the app partition?

Why this gate exists. CardSat moved from the stock `huge_app` scheme to a custom
partitions.csv (4 MB app, 1.5 MB LittleFS). With PartitionScheme=custom, arduino-cli
reports usage against the scheme's DECLARED ceiling in boards.txt -- 16 MB -- not
against the app0 region our partitions.csv actually defines:

    Sketch uses 3048390 bytes (18%) of program storage space. Maximum is 16777216 bytes.

18% is meaningless and 16 MB is not real: the chip has 8 MB and app0 is 4 MB. So the
compiler will happily produce a binary that cannot be flashed or booted, and the build
log will call it healthy. The old huge_app scheme did report the true limit, so this
check was not needed before; it is needed now precisely because the scheme changed.

Also verifies the layout itself: partitions must be ordered, non-overlapping, inside
the 8 MB part, and must still contain the two entries CardSat depends on --
  * a data/spiffs partition NAMED "spiffs", because LittleFS.begin(true) looks that
    label up by default and a rename silently mounts nothing;
  * a coredump partition, because the panic backtrace is read back on the next boot.

Usage:  check_app_fits.py [path-to-app.bin]
        with no argument it finds the newest CardSat.ino.bin in the build cache.
"""

import glob
import os
import sys

FLASH_BYTES = 8 * 1024 * 1024        # ESP32-S3FN8 on the Cardputer ADV
WARN_AT = 0.90                       # start warning before it is too late to react


def load_layout(path):
    parts = []
    for raw in open(path, encoding='utf-8'):
        line = raw.strip()
        if not line or line.startswith('#'):
            continue
        f = [x.strip() for x in line.split(',')]
        f = [x for x in f if x != '']
        if len(f) < 5:
            continue
        try:
            parts.append({'name': f[0], 'type': f[1], 'sub': f[2],
                          'off': int(f[3], 16), 'size': int(f[4], 16)})
        except ValueError:
            print(f'check_app_fits: FAIL -- unparseable line: {line}')
            sys.exit(1)
    return parts


def find_app_bin():
    cands = glob.glob('/root/.cache/arduino/sketches/*/CardSat.ino.bin')
    if not cands:
        return None
    return max(cands, key=os.path.getmtime)


def main() -> int:
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    csv = os.path.join(root, 'partitions.csv')
    if not os.path.exists(csv):
        print('check_app_fits: FAIL -- partitions.csv not found')
        return 1

    parts = load_layout(csv)
    problems = []

    # ---- layout sanity ---------------------------------------------------------
    prev = None
    for p in parts:
        if prev and p['off'] < prev['off'] + prev['size']:
            problems.append(f"{p['name']} at 0x{p['off']:X} overlaps "
                            f"{prev['name']} (ends 0x{prev['off'] + prev['size']:X})")
        prev = p
    end = prev['off'] + prev['size'] if prev else 0
    if end > FLASH_BYTES:
        problems.append(f'layout ends at 0x{end:X}, past the {FLASH_BYTES // (1024*1024)} MB part')

    if not any(p['name'] == 'spiffs' and p['sub'] == 'spiffs' for p in parts):
        problems.append('no data partition named "spiffs" -- LittleFS.begin(true) '
                        'would mount nothing and every setting would be lost')
    if not any(p['sub'] == 'coredump' for p in parts):
        problems.append('no coredump partition -- panic backtraces would be lost')

    app = next((p for p in parts if p['type'] == 'app'), None)
    if not app:
        problems.append('no app partition')

    # ---- does the binary fit? --------------------------------------------------
    binpath = sys.argv[1] if len(sys.argv) > 1 else find_app_bin()
    size = None
    if binpath and os.path.exists(binpath):
        size = os.path.getsize(binpath)
        if app and size > app['size']:
            problems.append(f'BINARY TOO LARGE: {size:,} bytes into a '
                            f"{app['size']:,} byte app partition "
                            f"({size - app['size']:,} over)")

    if problems:
        print('check_app_fits: FAIL')
        for p in problems:
            print('  ' + p)
        return 1

    free_mb = (FLASH_BYTES - end) / 1024 / 1024
    if size is None:
        print(f'check_app_fits: layout ok (app {app["size"]:,} B, '
              f'{free_mb:.2f} MB unallocated) -- no binary found to check')
        return 0

    pct = size / app['size']
    note = '   <-- approaching the limit' if pct >= WARN_AT else ''
    print(f'check_app_fits: ok  app {size:,} / {app["size"]:,} bytes '
          f'({pct * 100:.1f}%), {app["size"] - size:,} free{note}')
    print(f'                    {free_mb:.2f} MB of the part left unallocated '
          f'(Launcher and its data live there)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
