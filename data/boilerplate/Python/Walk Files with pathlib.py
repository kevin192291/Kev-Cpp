---
description: Goes through every matching file under a folder and its subfolders, skipping hidden ones like .git
order: 32
section: system
---
from pathlib import Path

# Sorted, so the order is the same on every run.
for path in sorted(Path(".").rglob("*.py")):
    if any(part.startswith(".") for part in path.parts):
        continue  # inside .git, .venv and other hidden folders
    print(path, path.stat().st_size)
