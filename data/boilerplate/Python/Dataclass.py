---
description: A dataclass: fields with types and defaults, and __init__, __repr__ and == written for you
ask: Name = Player
order: 20
section: data
---
from dataclasses import dataclass, field


# @dataclass(frozen=True) would make instances read-only and usable as dict keys.
@dataclass
class {{Name:pascal}}:
    name: str
    score: int = 0
    tags: list[str] = field(default_factory=list)  # a new list for each instance, not one shared list

    def add_points(self, points: int) -> None:
        self.score += points
