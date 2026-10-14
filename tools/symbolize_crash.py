#!/usr/bin/env python3
import bisect
import os
import re
import subprocess
import sys

LINE = re.compile(r'^(\S*?)\(\+0x([0-9a-f]+)\)')


def load_symbols(binary):
    output = subprocess.run(
        ['nm', '-n', '-S', '-C', '--defined-only', binary],
        check=True, capture_output=True, text=True).stdout
    starts = []
    items = []
    for line in output.splitlines():
        parts = line.split(None, 3)
        if len(parts) < 4 or parts[2] not in 'tTwW':
            continue
        start = int(parts[0], 16)
        size = int(parts[1], 16)
        starts.append(start)
        items.append((start, size, parts[3]))
    return starts, items


def resolve(starts, items, address):
    index = bisect.bisect_right(starts, address) - 1
    if index < 0:
        return None
    start, size, name = items[index]
    if size and address >= start + size:
        return None
    return '%s+0x%x' % (name, address - start)


def main():
    if len(sys.argv) != 3:
        print('usage: symbolize_crash.py <crash_trace> <unstripped binary>')
        return 2
    starts, items = load_symbols(sys.argv[2])
    module = os.path.basename(sys.argv[2]).lower()
    with open(sys.argv[1], encoding='utf-8', errors='replace') as trace:
        for line in trace:
            line = line.rstrip('\n')
            match = LINE.match(line)
            own = match and os.path.basename(match.group(1)).lower() == module
            name = own and resolve(starts, items, int(match.group(2), 16))
            print('%s  ->  %s' % (line, name) if name else line)
    return 0


if __name__ == '__main__':
    sys.exit(main())
