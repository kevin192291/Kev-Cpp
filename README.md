# Kev-C++

A lightweight Linux IDE for C, C++, JavaScript and TypeScript in the spirit of Dev-C++, built with
GTK 4. C++ projects build with CMake and g++, C projects with Make and gcc, and TypeScript projects
are served by a built-in dev server on port 5465.

## Requirements

Debian-family systems with GTK 4.10 or newer: Ubuntu 24.04 / Linux Mint 22 or newer, Debian 13,
Devuan 6 (and LCOS, which is built on it):

```sh
sudo apt install build-essential cmake ninja-build gdb git pkg-config \
                 libgtkmm-4.0-dev libgtksourceview-5-dev libvte-2.91-gtk4-dev \
                 nlohmann-json3-dev libgtest-dev clangd
```

Node.js 20 or newer (with npm) is also required. Debian 13 and Devuan 6 package it (`install.sh`
installs it from there); Ubuntu 24.04 / Mint 22 only package Node 18, so use
[nvm](https://github.com/nvm-sh/nvm) there. The build installs the TypeScript language server and
compiler into `tools/node_modules` with `npm ci` (pinned in `tools/package-lock.json`), which needs
internet access.

## Build, test, run

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build          # unit + integration tests (needs gcc, g++, make, cmake, git, node)
./build/kev-cpp [project-folder]
```

## Install

```sh
./install.sh                # installs into ~/.local; menu entry under Programming
./install.sh --system       # or for all users, into /usr/local (uses sudo)
./install.sh --uninstall
```

`install.sh` apt-installs any missing packages from the list above (and Node.js when the distribution's
is new enough), builds a Release copy in
`build/release` and installs it with its icon and menu entry. Re-run it after pulling changes to update
the installed copy. If `node` comes from a version manager such as nvm, the menu launcher is pointed at
that Node, because programs started from the menu don't get the PATH that `~/.bashrc` sets up.

It also installs `kev`, which opens a folder from a terminal and gives the terminal back:

```sh
kev ~/code/my-app    # its .kevc project if it has one; otherwise the folder itself
kev                  # the current folder
```

A folder with a `package.json` but no `.kevc` opens as a TypeScript project (Run serves it, Compile
type-checks it) without Kev-C++ writing anything into it.

## Features

- **Projects**: File → New Project creates C++ (console, static or shared library), C console or
  TypeScript web projects. Each has a `<name>.kevc` project file.
- **Managed build files**: Kev-C++ owns one marked block in `CMakeLists.txt` / `Makefile` and keeps it
  in sync. New, deleted and renamed source files are added and removed automatically. Everything
  outside the block is yours and is never touched.
- **Project Options** (Alt+P): language standard, warnings, optimization, defines, include and library
  directories, libraries and linker flags.
- **Packages** (Project → Packages): add libraries from a catalog (fmt, spdlog, nlohmann_json, CLI11,
  magic_enum, glm, googletest, Catch2, glfw, raylib, SDL3) or any Git repository via CMake
  FetchContent ("Detect Targets" finds what to link), or system libraries via pkg-config.
- **IntelliSense**: completion, hover, go to definition (Ctrl+click / Ctrl+F12), live diagnostics and a
  symbol list, from clangd (C/C++) and typescript-language-server (JS/TS).
- **Build & run**: errors and warnings are listed in the Compiler tab (double-click to jump); programs run in
  the Run tab; the Terminal tab is a shell in the project folder.
- **Slate colours**: the dark slate palette of Embarcadero Dev-C++ 6, for the window and the editor,
  whatever the desktop theme is.
- **Dev-C++-style toolbar**: pixel icons in the Dev-C++ style (see [docs/ICONS.md](docs/ICONS.md)). Each
  button that starts something (Compile, Run, Compile & Run, Rebuild All, Clean, Debug, the dev server)
  turns into a Stop button while it runs; Continue turns into Pause while the debugged program runs.
- **Movable panels**: every bottom panel can be docked at the bottom, docked on the right, or opened in
  its own window. Drag its tab, or use the ⋮ menu on the tab. Closing a panel window docks its panels
  back at the bottom. The layout is remembered (`~/.config/kev-cpp/layout.json`); View → Reset Panel
  Layout puts everything back.
- **Debugger** (gdb): click a line number to set a breakpoint; stepping, call stack, locals, watches and a gdb
  console. Hover a variable while paused to see its value; pointers are followed automatically
  (`p = 0x7ffe… → {x = 3, y = 4}`).
- **TypeScript dev server**: serves the project on http://127.0.0.1:5465/ (this computer only),
  converts `.ts` to JavaScript on request, and reloads the browser on save. Compile (F9) type-checks
  with `tsc --noEmit`.

## Keyboard shortcuts

| Key | Action | Key | Action |
|---|---|---|---|
| F9 | Compile (TypeScript: type-check) | F5 | Start debugging |
| F10 | Run (TypeScript: serve + open browser) | F6 | Step over |
| F11 | Compile & run | F7 | Step into |
| F12 | Rebuild all | Shift+F7 | Step out |
| | | F8 | Continue |
| Ctrl+F2 | Stop build or program | Shift+F5 | Stop debugging |
| Alt+P | Project Options | Ctrl+F5 | Toggle breakpoint |
| Ctrl+Space | Completion | Ctrl+F12 | Go to definition |

Logs from the language servers are in `~/.cache/kev-cpp/logs/`.
