# Kev-C++

A lightweight Linux IDE for C, C++, JavaScript and TypeScript in the spirit of Dev-C++, built with
GTK 4. C++ projects build with CMake and g++, C projects with Make and gcc, and TypeScript projects
are served by a built-in dev server on port 5465.

## Requirements

Ubuntu 24.04 / Linux Mint 22 or newer:

```sh
sudo apt install build-essential cmake ninja-build gdb git pkg-config \
                 libgtkmm-4.0-dev libgtksourceview-5-dev libvte-2.91-gtk4-dev \
                 nlohmann-json3-dev libgtest-dev clangd
```

Node.js 20 or newer (with npm) is also required. The build installs the TypeScript language server and
compiler into `tools/node_modules` with `npm ci` (pinned in `tools/package-lock.json`).

## Build, test, run

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build          # unit + integration tests (needs gcc, g++, make, cmake, git, node)
./build/kev-cpp [project-folder]
```

To install it into your applications menu (with its icon):

```sh
cmake --install build --prefix ~/.local     # re-run after rebuilding to update the installed copy
```

Uninstall with `xargs rm < build/install_manifest.txt`.

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
