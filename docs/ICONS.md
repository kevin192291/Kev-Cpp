# Icons

Kev-C++ uses Dev-C++-style pixel icons. The source art is `data/art/icon-sprite-sheet.png` (an 8×8 grid)
and `data/art/classic-toolbar-sheet.png` (the classic Dev-C++ toolbar, 8×4; see below);
`data/art/make_icons.py` cuts each icon out, removes the semi-transparent halo around it, draws the
few icons the sheet lacks, and writes 16/24/32/48 px PNGs to `data/icons/hicolor/<size>/actions/` plus
`data/kevcpp-ui-icons.gresource.xml`. The output is committed, so building needs neither Python nor
Pillow. After changing the sheet or the script:

```sh
python3 data/art/make_icons.py --preview /tmp/icons.png   # the preview is a contact sheet to check
```

The icons are embedded in the binary (GResource) and looked up by name, e.g. `kevc-run`.
`src/core/Icons.h` picks the icon for a file, code symbol or debugger value.

## The sprite sheet

Rows and columns as on the sheet; the icon is `kevc-<name>`.

| # | Row, col | Meaning | Looks like | Name |
|---|---|---|---|---|
| 1 | 1, 1 | Integer variable | Blue cube marked i | `integer-variable` |
| 2 | 1, 2 | Floating-point variable | Red cube marked f | `float-variable` |
| 3 | 1, 3 | String variable | Silver block marked abc | `string-variable` |
| 4 | 1, 4 | Boolean variable | Blue cube with green check mark | `bool-variable` |
| 5 | 1, 5 | Character variable | Blue cube with quoted a badge | `char-variable` |
| 6 | 1, 6 | Array variable | Three blue blocks inside square brackets | `array-variable` |
| 7 | 1, 7 | Pointer variable | Blue cube with arrow and asterisk | `pointer-variable` |
| 8 | 1, 8 | Constant variable | Blue cube with yellow padlock | `const-variable` |
| 9 | 2, 1 | Global variable | Globe with small blue cube | `global-variable` |
| 10 | 2, 2 | Local variable | Blue cube on gray stack | `local-variable` |
| 11 | 2, 3 | Function parameter | Green arrow pointing into blue cube | `parameter` |
| 12 | 2, 4 | Member variable | Purple block beside small blue cube | `member-variable` |
| 13 | 2, 5 | Static variable | Blue cube with gray pin | `static-variable` |
| 14 | 2, 6 | Enumeration | Red, green and yellow value markers | `enum` |
| 15 | 2, 7 | Structure | Connected blue blocks inside curly braces | `struct` |
| 16 | 2, 8 | Class | Cluster of three purple blocks | `class` |
| 17 | 3, 1 | Public member | Blue cube with green plus badge | `public-member` |
| 18 | 3, 2 | Protected member | Blue cube with gold key | `protected-member` |
| 19 | 3, 3 | Private member | Blue cube with red padlock | `private-member` |
| 20 | 3, 4 | Function | Document marked f() | `function` |
| 21 | 3, 5 | Method | Purple block with f() badge | `method` |
| 22 | 3, 6 | Constructor | Purple block with hammer | `constructor` |
| 23 | 3, 7 | Namespace | Yellow folder marked with curly braces | `namespace` |
| 24 | 3, 8 | Header file | Document marked .h | `header-file` |
| 25 | 4, 1 | C++ source file | Document marked C++ | `cpp-file` |
| 26 | 4, 2 | C source file | Document marked C | `c-file` |
| 27 | 4, 3 | Resource file | Document with small landscape picture | `resource-file` |
| 28 | 4, 4 | Project | Yellow folder with gray gear | `project` |
| 29 | 4, 5 | Workspace or solution | Three connected yellow folders | `workspace` |
| 30 | 4, 6 | Closed folder | Closed yellow folder | `folder` |
| 31 | 4, 7 | Open folder | Open yellow folder | `folder-open` |
| 32 | 4, 8 | Generic document | Blank document with folded corner | `document` |
| 33 | 5, 1 | New file | Blank document with yellow sparkle | `new-file` |
| 34 | 5, 2 | Open file | Open yellow folder with blue arrow | `open` |
| 35 | 5, 3 | Save | Blue floppy disk | `save` |
| 36 | 5, 4 | Save all | Two overlapping blue floppy disks | `save-all` |
| 37 | 5, 5 | Print | Gray printer with paper | `print` |
| 38 | 5, 6 | Cut | Red-handled scissors | `cut` |
| 39 | 5, 7 | Copy | Two overlapping documents | `copy` |
| 40 | 5, 8 | Paste | Clipboard with document | `paste` |
| 41 | 6, 1 | Undo | Blue curved arrow pointing left | `undo` |
| 42 | 6, 2 | Redo | Blue curved arrow pointing right | `redo` |
| 43 | 6, 3 | Find | Binoculars | `find` |
| 44 | 6, 4 | Find in files | Binoculars over yellow folder | `find-in-files` |
| 45 | 6, 5 | Replace | Letters a and b with swapping arrows | `replace` |
| 46 | 6, 6 | Settings | Gray gear | `settings` |
| 47 | 6, 7 | Help | Yellow question mark | `help` |
| 48 | 6, 8 | About | White i on blue circle | `about` |
| 49 | 7, 1 | Compile | Source document with hammer | `compile` |
| 50 | 7, 2 | Build project | Yellow folder with hammer | `build-project` |
| 51 | 7, 3 | Rebuild all | Stack of documents with hammer | `rebuild` |
| 52 | 7, 4 | Clean | Yellow broom | `clean` |
| 53 | 7, 5 | Run | Green right-pointing triangle | `run` |
| 54 | 7, 6 | Stop | Red square | `stop` |
| 55 | 7, 7 | Pause | Two blue vertical bars | `pause` |
| 56 | 7, 8 | Debug | Green bug | `debug` |
| 57 | 8, 1 | Toggle breakpoint | Document with red breakpoint dot | `breakpoint` |
| 58 | 8, 2 | Step into | Document with blue downward arrow | `step-into` |
| 59 | 8, 3 | Step over | Document with curved blue arrow | `step-over` |
| 60 | 8, 4 | Step out | Document with blue upward arrow | `step-out` |
| 61 | 8, 5 | Watch | Eyeglasses beside blue cube | `watch` |
| 62 | 8, 6 | Call stack | Three overlapping gray stack frames | `call-stack` |
| 63 | 8, 7 | Memory | Black chip with gold pins | `memory` |
| 64 | 8, 8 | Error list | Document with red X and gray lines | `error-list` |

## The classic Dev-C++ toolbar sheet

`classic-toolbar-sheet.png`, 8 icons across and 4 down. Its rows aren't evenly spaced, so the script
finds each icon's cell from the transparent gaps between them. The icon is `kevc-classic-<name>`;
"Toolbar" says which button uses it.

| # | Row, col | Meaning | Looks like | Name | Toolbar |
|---|---|---|---|---|---|
| 1 | 1, 1 | New file | Blank page with a folded corner | `new-file` | New Source File |
| 2 | 1, 2 | Open file | Open yellow folder | `open-file` | Open Project or Folder |
| 3 | 1, 3 | Save | Grey floppy disk | `save` | Save |
| 4 | 1, 4 | Save all | Two grey floppy disks | `save-all` | Save All |
| 5 | 1, 5 | Close file | Lined page with a red X | `close-file` | Close File |
| 6 | 1, 6 | Print | Grey printer with paper | `print` | (no printing yet) |
| 7 | 1, 7 | Undo | Blue arrow curving left | `undo` | Undo |
| 8 | 1, 8 | Redo | Blue arrow curving right | `redo` | Redo |
| 9 | 2, 1 | Find | Lined page with a magnifying glass | `find` | Find |
| 10 | 2, 2 | Find previous | Magnifying glass, green left arrow | `find-previous` | |
| 11 | 2, 3 | Find next | Magnifying glass, green right arrow | `find-next` | |
| 12 | 2, 4 | Replace | Lined page with a yellow highlight | `replace` | Replace |
| 13 | 2, 5 | Go to line | Green arrow into a page | `go-to-line` | Go to Line |
| 14 | 2, 6 | Project options | Page with green squares and lines | `project-options` | Project Options |
| 15 | 2, 7 | Compiler options | Orange and gold settings disc | `compiler-options` | |
| 16 | 2, 8 | New project | Gold shield | `new-project` | New Project |
| 17 | 3, 1 | Compile | Green, red, yellow and blue squares | `compile` | Compile |
| 18 | 3, 2 | Run | Empty window with a gold title bar | `run` | Run |
| 19 | 3, 3 | Compile and run | Window with the four coloured squares | `compile-and-run` | Compile & Run |
| 20 | 3, 4 | Rebuild all | Four pale grey squares | `rebuild-all` | Rebuild All |
| 21 | 3, 5 | Debug | Purple check mark | `debug` | Debug |
| 22 | 3, 6 | Help | Yellow question mark | `help` | |
| 23 | 3, 7 | Tips | Yellow smiling face | `tips` | |
| 24 | 3, 8 | Resources | Red, blue and yellow pages | `resources` | |
| 25 | 4, 1 | Insert | Lined page with an insertion arrow | `insert` | |
| 26 | 4, 2 | Toggle | Gold puzzle piece with a green plus | `toggle` | Toggle Breakpoint |
| 27 | 4, 3 | Go to | Blue lined page | `go-to` | |
| 28 | 4, 4 | Project folder | Yellow folder | `project-folder` | |
| 29 | 4, 5 | Source file | Page with code punctuation | `source-file` | |
| 30 | 4, 6 | Resource file | Page with a grid of dots | `resource-file` | |
| 31 | 4, 7 | Compile log | Green, red, yellow and blue bars | `compile-log` | |
| 32 | 4, 8 | Application | Blue monitor on a stand | `application` | |

Buttons the classic toolbar didn't have (Find in Files, Clean, Continue/Pause, the step buttons, Add
Watch and the web group) keep the icons from `icon-sprite-sheet.png`, as do the panel tabs and menus.

## Made for Kev-C++ (same style)

| Name | Used for | How it's made |
|---|---|---|
| `compile-run` | Compile & Run | Compile with a Run triangle badge |
| `continue` | Continue (debugger) | One Pause bar and the Run triangle |
| `new-project` | New Project | Project folder with the New file sparkle |
| `error`, `breakpoint-dot` | Compiler errors, breakpoint gutter mark | The red badges of Error list and Toggle breakpoint |
| `breakpoint-if` | IF breakpoint (gutter, Breakpoints tab) | Drawn: red circle with a white ? |
| `breakpoint-live` | Live breakpoint | Drawn: teal diamond |
| `breakpoint-change` | Break when a value changes | Drawn: amber circle with Δ |
| `warning` | Compiler warnings | Drawn: yellow triangle with ! |
| `globe` | Dev server | Drawn |
| `servers` | Running Servers (web toolbar, SvelteKit) | The globe with the Stop square as a badge |
| `terminal` | Terminal and GDB Console tabs | Drawn |
| `exec-arrow` | Current line while debugging | Drawn: green arrow |
| `pinned`, `unpinned` | Welcome tab: pinned and not pinned projects | Drawn: red pushpin, and the same in light grey |
| `git-modified`, `git-added`, `git-conflict` | Project tab: uncommitted changes, new files, merge conflicts | Drawn: amber dot, green plus, red ! (half-size badges) |
| `ts-file`, `js-file` | TypeScript and JavaScript files | The C++ page relabelled |
| `svelte-file` | Svelte components (`.svelte`), the Routes tab | The C++ page relabelled "SV" in Svelte orange |
| `shared-library` | New Project: C++ Shared Library | Generic document with two Settings gears, like the Windows DLL icon |
| `static-library` | New Project: C++ Static Library | Copy (two pages) with a Settings gear |

Notes use `about`.

## Language logos

The New Project dialog shows the languages' own logos for the C++, C, TypeScript and SvelteKit project
types. They are in `data/art/logos/`, scaled down to 512 px from Wikimedia Commons. The C++, C and
TypeScript logos are in the public domain there and the Svelte logo is under the MIT licence (all are
trademarks of their owners):

| Name | File | Source |
|---|---|---|
| `cpp-logo` | `cpp.png` | [ISO_C++_Logo.svg](https://commons.wikimedia.org/wiki/File:ISO_C%2B%2B_Logo.svg) |
| `c-logo` | `c.png` | [C_Programming_Language.svg](https://commons.wikimedia.org/wiki/File:C_Programming_Language.svg) |
| `ts-logo` | `typescript.png` | [Typescript.svg](https://commons.wikimedia.org/wiki/File:Typescript.svg) |
| `svelte-logo` | `svelte.png` | [Svelte_Logo.svg](https://commons.wikimedia.org/wiki/File:Svelte_Logo.svg) (MIT) |

## Start/Stop buttons

A toolbar button that starts something (Compile, Run, Compile & Run, Rebuild All, Clean, Debug, the
dev server) shows `stop` while that is running and stops it when clicked; Continue shows `pause`
while the program being debugged runs. See `TaskButton` (src/ui/IconWidgets.h) and
`MainWindow::updateToolbar()`.
