---
description: Runs a program, collects its output and stops with an error if it fails
order: 33
section: system
---
import subprocess

# A list rather than one string: no shell, so no quoting problems with spaces in names.
# check=True raises subprocess.CalledProcessError when the program exits with a non-zero status.
result = subprocess.run(["git", "status", "--short"], capture_output=True, text=True, check=True)
for line in result.stdout.splitlines():
    print(line)
