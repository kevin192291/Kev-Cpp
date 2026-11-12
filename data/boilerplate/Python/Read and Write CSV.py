---
label: Read/Write CSV
description: read_csv() gives each row as a dict keyed by the header line; write_csv() writes them back
order: 22
section: data
---
import csv
from pathlib import Path


# newline="" lets the csv module deal with line breaks itself, including those inside quoted fields.
def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def write_csv(path: Path, rows: list[dict[str, str]]) -> None:
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    with path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
