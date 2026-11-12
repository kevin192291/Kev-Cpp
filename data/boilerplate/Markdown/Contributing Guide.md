---
description: A CONTRIBUTING.md: building, code style, commits, pull requests and bug reports
file: CONTRIBUTING.md
whole-file: yes
order: 12
section: documents
---
# Contributing

Thanks for helping! This page explains how to build the project and what a change needs before it's
merged.

## Building and testing

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## Code style

- Indent with 4 spaces; keep lines under 120 columns.
- Format what you change with the project's `.clang-format` (in Kev-C++: select it, then Ctrl+I).
- Comments explain *why*, not what the next line does.
- New behaviour comes with a test.

## Commits

- One logical change per commit.
- First line: a short summary in the imperative ("Fix crash when the file is empty"), under 72
  characters; details after a blank line.

## Pull requests

1. Fork the repository and create a branch from `main`.
2. Make the change, with tests; check that everything builds and passes.
3. Open a pull request saying what it changes and why, and link the issue it fixes.

## Reporting bugs

Open an issue with the steps to reproduce it, what you expected, what happened instead, and your
system (distribution, compiler version).
