---
description: A command-line tool whose arguments (files, -o, -n, -v, --help) are read with argparse
file: main.py
whole-file: yes
order: 11
section: starters
---
#!/usr/bin/env python3
"""Reads the given files and reports on them."""

import argparse
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+", type=Path, help="files to read")
    parser.add_argument("-o", "--output", type=Path, help="write the report here instead of to the screen")
    parser.add_argument("-n", "--count", type=int, default=10, help="how many lines (default: %(default)s)")
    parser.add_argument("-v", "--verbose", action="store_true", help="say what is happening")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    for path in args.files:
        if not path.is_file():
            print(f"{path}: no such file", file=sys.stderr)
            return 1
        if args.verbose:
            print(f"reading {path}", file=sys.stderr)
        {{cursor}}
    return 0


if __name__ == "__main__":
    sys.exit(main())
