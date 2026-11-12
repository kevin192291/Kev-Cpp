# Boilerplate

Everything in Kev-C++'s **Boilerplate** menu is a file in a folder like this one. The folders are the
submenus and each file is one item: `C and C++/SFML/Window.cpp` is Boilerplate → C/C++ → SFML → Window.
Choosing an item puts the file's text into the editor at the cursor (replacing the selection), as one
step that Ctrl+Z takes back.

Two folders are read and merged:

- the built-in one, installed with Kev-C++ (`share/kev-cpp/boilerplate`; `data/boilerplate` in the
  source tree);
- your own, `~/.config/kev-cpp/boilerplate` (Boilerplate → Open My Boilerplate Folder makes it).
  A file at the same place as a built-in one replaces it.

**Boilerplate → Save Selection as Boilerplate…** writes the selected text into your folder for you. To
add one by hand, drop a file into a folder. The menu picks up new files when you come back to the
Kev-C++ window, or when you save a file in one of these folders in Kev-C++. Changes to a file's text
apply the next time you choose it.

## The file

- **Its name is the menu item**: `Custom Drawable Class.cpp` shows as "Custom Drawable Class".
- **Its extension is its language.** It decides where the text can go: a `.cpp` snippet goes into C++
  files, a `.css` one into CSS, Svelte and HTML files, a `.txt` one anywhere. When the open file
  doesn't fit, Kev-C++ offers to put the text into a new file instead.
- Files and folders whose names start with `_` or `.` aren't items (this file, `_folder.txt`).

The file may start with a header between two `---` lines. Every line in it is optional:

```
---
label: Read/Write a File
order: 10
section: Starters
description: Opens a file, reads it line by line and writes a copy
file: {{ClassName}}.hpp
whole-file: yes
ask: ClassName = Player
packages: sfml-graphics sfml-audio
includes: <SFML/Graphics.hpp> <vector>
sfml: 3
---
the text that gets inserted
```

| Key | Meaning |
| --- | --- |
| `label` | The menu text, when the file name can't say it (a `/`, for example). |
| `order` | Lower numbers come first; the default is 100. Items with the same order are alphabetical. |
| `section` | Items next to each other with different sections get a separator between them. |
| `description` | One line, shown in the status bar after inserting. |
| `file` | The name suggested when the text goes into a new file. With a `/` it's relative to the project folder; without one it goes beside the open file. The default is the snippet's own file name. |
| `whole-file` | `yes` for a whole file (a program, a component, a config). When the open file already has text, Kev-C++ asks whether to replace it, insert at the cursor, or make a new file. |
| `ask` | A value to ask for before inserting: `ask: Name = Default`. One line each; asked in order. |
| `packages` | pkg-config modules a C/C++ project needs for this code. They're added to the project's packages when Kev-C++ manages its CMakeLists.txt (SFML's the way the installed SFML links best). |
| `includes` | Headers the code needs, `<vector>` or `"player.h"`. The ones a C/C++ file lacks are added after its last `#include`. Not for whole files, which have their own. |
| `sfml` | `2` or `3`: the SFML version the code is written for. It's only listed when that's the SFML installed (3 when there's none). |

## Placeholders

| Placeholder | Becomes |
| --- | --- |
| `{{cursor}}` | Where the cursor goes afterwards (otherwise the end). |
| `{{selection}}` | The text that was selected, re-indented to fit (for "wrap in…" snippets). |
| `{{Name}}` | A value from an `ask:` line. |
| `{{file}}`, `{{fileStem}}` | The file that was open when you chose the item (`player.cpp`), and its name without the extension (`player`). Also when the text goes into a new file: `file: {{fileStem}}.test.ts` names a test after the code it tests. |
| `{{project}}` | The project folder's name. |
| `{{date}}`, `{{year}}` | Today, as `2026-11-12`, and `2026`. |

Add `:upper`, `:lower`, `:snake`, `:kebab`, `:camel` or `:pascal` to change a value's case, one after
another: `{{ClassName:snake:upper}}` turns `PlayerShip` into `PLAYER_SHIP`. Anything else in double
braces is left alone, so `${name}`, `{{1, 2}, {3, 4}}` and Svelte's `{value}` are safe.

The text is indented to match the line it goes into, so write it starting at the left margin. A final
newline is dropped when inserting at the cursor and kept for a whole new file; code after the cursor
moves to a line of its own below the inserted text.

## A folder

A folder may hold `_folder.txt` with the same `label`, `order` and `section` lines (no `---` needed):

```
label: C/C++
order: 10
```
