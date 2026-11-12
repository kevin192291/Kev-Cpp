---
description: A Python script with a docstring, a main() returning the exit status and the __main__ guard
file: main.py
whole-file: yes
order: 10
section: starters
---
#!/usr/bin/env python3
"""One line saying what this script does."""

import sys


def main() -> int:
    {{cursor}}
    return 0


if __name__ == "__main__":
    sys.exit(main())
