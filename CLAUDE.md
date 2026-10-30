# Kev-C++ — notes for Claude (and humans)

A Dev-C++-style IDE for Linux (GTK 4 / gtkmm 4.10, C++20). C++ projects build with **CMake + g++**,
C projects with **Make + gcc**, TypeScript projects are served by a Node dev server on **port 5465**.
See README.md for features and keys; docs/PLAN.md is the original plan the code was built from.
The repo folder is still called `dev-k++` (the project was renamed to Kev-C++ partway through).

## Build & test

```sh
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build   # 75 tests
cmake --install build --prefix ~/.local    # updates the copy in the user's applications menu
```

Tests: GoogleTest for `src/core` (headless, includes real cmake/g++/make/gcc/gdb/tsc/git runs) plus
`node --test tools/devserver/devserver.test.mjs` (registered in ctest). Everything GTK-dependent is
verified by driving the real app (see "GUI testing").

## Layout

- `src/core/` — **no GTK**; all unit-tested. Project model (`.kevc` JSON), `ManagedBlock` (the
  marker-delimited block we own inside CMakeLists.txt/Makefile), `BuildFiles` (CMake/Make generators,
  compile_commands.json for C), `BuildPlan` (commands per action/config), `Diagnostics` (gcc/ld/CMake/
  make/tsc output parser), `Packages` (catalog, pkg-config, FetchContent target detection), `Lsp`
  (framing, URIs, UTF-16 columns, markdown→Pango), `Mi` (GDB/MI parser, expression-under-cursor).
- `src/app/` — `Application` (startup, toolchain check, accelerators, CSS, icon), `MainWindow`
  (everything wired together: menus, actions, build/run, debugging, IntelliSense, dev server).
- `src/editor/` EditorTab (GtkSourceView; breakpoints = source marks), ProjectTree, SymbolsPanel.
- `src/lsp/` LspClient (JSON-RPC over stdio), LspManager (documents ↔ servers), LspBridges
  (GObject completion/hover providers for GtkSourceView, written in C style).
- `src/debug/` GdbSession (MI process), DebugController (session state), DebugViews (VarNode tree,
  Debug panel, Watches), DebugHover (hover provider).
- `src/build/` BuildRunner (sequential subprocesses, line streaming), CompilerPanel.
- `src/ui/` Dialogs, ProjectOptionsDialog (compiler/linker/packages), DockManager (panels docked
  bottom/right/floating; layout in `~/.config/kev-cpp/layout.json`).
- `src/devserver/DevServer` runs `tools/devserver/devserver.mjs`; `tools/package.json` pins
  typescript 5.9.3 + typescript-language-server 5.3.0 (installed by `npm ci` during the build).
- `data/` icons (GResource-embedded), `packages.json` catalog, `.desktop` template.

## Decisions worth keeping

- **Build files**: the IDE owns only the `# >>> Kev-C++ managed` … `# <<< Kev-C++ managed` block and
  regenerates it from the `.kevc`; user text outside it is preserved byte-for-byte (tested). Sources
  are listed explicitly (no GLOB). `SourceWatcher` resyncs on file create/delete/rename (250 ms debounce).
- **IntelliSense** = clangd (`--query-driver=gcc,g++` so gcc's headers are used) + typescript-language-server.
  Completion follows GtkSourceView's model: populate returns SortListModel(FilterListModel(store)),
  refilter only updates the filter. When the server says `isIncomplete`, refilter re-requests.
  Ranking: typed-prefix first, then shortest match, then the server's sortText (otherwise clangd
  buries e.g. `std::cout` in files without `#include <iostream>`). clangd restarts once
  compile_commands.json appears. LSP hover is suppressed while the debugger is paused.
- **Debugger** = gdb `--interpreter=mi3` with `mi-async on`, `-enable-pretty-printing`, debuginfod
  off, program I/O on a VTE pty (`-inferior-tty-set`). C++ struct children come under
  `public/private/protected` pseudo-children: flattened. Pointers (not char*/void*) show their
  pointee via `-var-info-path-expression` + `-data-evaluate-expression *(…)`. Expanded rows are
  remembered by path across steps (`ExpandedRows`). Keys: F5 debug, F6 over, F7 into, Shift+F7 out,
  F8 continue, Shift+F5 stop; click a line number for a breakpoint.
- **Dev server**: loopback only, `..` rejected with 403, bare npm imports rewritten to an explanatory
  module, exits code 2 if the port is taken, and exits when its stdin pipe closes (IDE crash).
- **Keys GTK steals**: F10 opens the menubar and Alt+letter triggers mnemonics, so MainWindow claims
  F10 and Alt+P in a capture-phase key controller.
- **Async safety**: callbacks from gdb/LSP/processes capture a `weak_ptr` token, never raw `this`
  alone (`sigc::track_obj` only protects sigc slots, not `std::function`).

## GUI testing (important)

- Never send synthetic input to the user's real display (:0). Run `Xephyr :5 -screen 1400x950` and
  start the app with `DISPLAY=:5 GDK_BACKEND=x11 XDG_CONFIG_HOME=<tmp> XDG_CACHE_HOME=<tmp>` so test
  runs don't touch the user's panel layout or caches. Drive it with python-xlib XTest; screenshot with
  `root.get_image`. No window manager in Xephyr: windows open at (0,0), focus follows the pointer.
- Never trigger file dialogs (Ctrl+O, Browse…) in tests: GTK routes them through the desktop portal,
  so they appear on the user's real screen and block the app. Open projects via the command line.
- Don't press F10/Open in Browser on web projects in tests (it opens the user's real browser).
- Dropping a dragged tab on the bare X11 root window hangs GTK 4's drag (GDK_DRAG_PROTO_ROOTWIN);
  in-app drops are caught by `DockManager::acceptFloatingDrops`.

## State and known gaps

All seven planned milestones are done (projects, build/run, options & packages, IntelliSense,
debugger, dev server) plus docking panels. Not done yet: Replace (Ctrl+R) and Find Results are
placeholders; breakpoints/watches aren't saved between sessions; the dev server can't resolve npm
packages; floating panel window positions aren't remembered (GTK 4 can't position windows).

## Conventions

C++20, 4-space indent, `kevc` namespace, member suffix `_`, comments explain *why*. New logic goes in
`src/core` with tests whenever it can be GTK-free. Commit messages end with the Co-Authored-By line.
