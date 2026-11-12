---
label: Read/Write JSON
description: load_json() and save_json(), which never leaves a half-written file behind
order: 21
section: data
---
import json
from pathlib import Path
from typing import Any


def load_json(path: Path) -> Any:
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def save_json(path: Path, data: Any) -> None:
    # Written to a temporary file that then replaces the old one, so a crash can't leave half a file.
    tmp = path.with_name(path.name + ".tmp")
    with tmp.open("w", encoding="utf-8") as f:
        json.dump(data, f, indent=4, ensure_ascii=False)
        f.write("\n")
    tmp.replace(path)
