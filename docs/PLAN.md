# Kev-C++ — first-pass plan

> The plan as approved at the start of development, kept for history. The code has since moved on in
> places (e.g. a separate Run tab, debugger keys F6/F7/F8, docking panels); CLAUDE.md describes the
> current design.

## Context
This was a new project; the folder was empty and not a git repo yet. The goal is a
Linux-native IDE in the spirit of Dev-C++, covering C, C++, JS and TS. It should have syntax highlighting, IntelliSense,
one-key compile and run, an IDE-managed project and build file, a dialog for adding packages and linker settings, and a
debugger. While debugging, hovering over a variable shows its value, and pointers are followed to the value they point at
(like Visual Studio's hover tooltips).

Decisions from the user:
- **Build systems:** every C++ project uses **CMake**, and every C project uses **Make**. There is no single-file mode.
  gcc/g++ are the only compilers. The IDE adds new source files to the build file automatically.
- **Packages:** C++ projects pull packages with CMake's FetchContent through a dialog. The same dialog manages
  linker settings.
- **IntelliSense:** an LSP client now (clangd for C/C++, typescript-language-server for JS/TS), kept behind an
  interface so we can add our own engine later.
- **UI:** GTK4 + gtkmm-4.0, written in C++20 and built with CMake. GtkSourceView 5 and VTE are used through
  their C APIs, since Ubuntu 24.04 ships no gtksourceviewmm-5.
- **Port 5465:** a dev server that serves the project, converts `.ts` to `.js` when requested, and live-reloads
  the browser. It binds to 127.0.0.1 only. **Node** is a hard dependency.

Environment (Mint 22.2 / Ubuntu 24.04): gcc/g++ 13.3, cmake 4.2, make, gdb 15.1, node 22.14, npm 11 are present.
To install:
```
sudo apt install libgtkmm-4.0-dev libgtksourceview-5-dev libvte-2.91-gtk4-dev \
                 nlohmann-json3-dev libgtest-dev clangd ninja-build git
```
Startup toolchain check: **gcc, g++, cmake, make, gdb, node** are required, and the IDE shows an error dialog and quits
if any is missing. **clangd and git** are optional: if missing, it shows a warning and turns off IntelliSense or FetchContent.

## Project model (like Dev-C++'s `.dev` file)
- Each project has a `<name>.kevc` JSON file. It records the kind (cpp-cmake | c-make | web-ts), the target type
  (console | static | shared), the language standard, the source list, defines, include dirs, compiler flags,
  link libraries, link dirs, linker flags, and packages.
- The IDE **owns a marked block** inside the real build file and regenerates it from `.kevc` whenever the model
  changes. Anything outside the markers belongs to the user and is kept byte-for-byte:
  ```cmake
  # >>> Kev-C++ managed — edits inside this block are overwritten
  target_sources(app PRIVATE src/main.cpp src/foo.cpp include/foo.hpp)
  FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG 11.0.2 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(fmt)
  target_link_libraries(app PRIVATE fmt::fmt m)
  # <<< Kev-C++ managed
  ```
  The Makefile uses the same scheme, with managed `SRCS`, `CFLAGS`, `LDFLAGS`, `LDLIBS`, and `PKGS` (pkg-config)
  variables above a fixed set of rules. It builds into `build/`, uses `-MMD -MP` dependency tracking, and supports
  `DEBUG=1`.
- **Automatic source adding.** A `Gio::FileMonitor` watches the project tree, skipping `build/`, `.kevc/`, `_deps/`
  and dot-directories. Files created through the IDE's New File command, or from outside it, are added. Deleted
  files are removed, and renamed files are updated. C++ projects track `.cpp .cc .cxx .c .h .hpp .hh .hxx .inl .tpp`,
  and C projects track `.c .h`. Sources are listed explicitly rather than with `GLOB` so CMake re-configures
  correctly. Each change triggers a debounced regeneration of the build file.
- **Compiler paths.** CMake is configured with `-DCMAKE_C_COMPILER=<gcc> -DCMAKE_CXX_COMPILER=<g++>
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`, using `build/debug` and `build/release` directories. For Make projects, the
  IDE writes `compile_commands.json` itself from the model, since it already knows every flag, so clangd works
  without `bear`.
- Opening a CMake/Make folder that has no `.kevc` file offers to "Enable Kev-C++ management", which adds a
  `.kevc` file and a managed block.
- The New Project wizard offers templates for C++ Console, C++ Static Lib, C++ Shared Lib, C Console and TS Web App.

## Project Options dialog (Alt+P)
- **General:** name, target type, output name, C/C++ standard.
- **Compiler:** defines, include dirs, warnings, extra flags, debug/release optimization levels.
- **Linker:** libraries (`-l`), library dirs, extra linker flags, static linking of libstdc++/libgcc.
- **Packages** (C++):
  - *Catalog* comes from a bundled `data/packages.json` listing name, git URL, pinned tag and CMake targets.
    It covers fmt, spdlog, nlohmann_json, GoogleTest, Catch2, CLI11, glm, magic_enum, glfw, raylib and SDL3.
    Selecting a package adds the FetchContent declaration and links its targets automatically.
  - *Custom Git* takes a URL, tag and target names, and has a "Detect targets" button that configures the
    package in a scratch build and lists the targets it exports.
  - *System* lists `pkg-config --list-all`, and a chosen package becomes `pkg_check_modules(... IMPORTED_TARGET)`.
- **Packages** (C): only the System (pkg-config) tab, which feeds into `PKGS`.
- The generated CMake block sets `CMAKE_POLICY_VERSION_MINIMUM 3.5`. Without it, CMake 4.x refuses older
  packages that declare `cmake_minimum_required` below 3.5.

## Debugger (gdb/MI)
- The IDE runs `gdb --interpreter=mi3` and talks to it through GDB/MI, the established machine interface, whose
  variable objects map directly onto expandable trees. Pretty-printing is turned on with `-enable-pretty-printing`,
  so the libstdc++ printers make `std::vector`, `std::string`, smart pointers and so on display properly.
  The program's input and output go to the VTE terminal through `-inferior-tty-set <vte pty>`.
- Breakpoints are toggled by clicking the gutter, using GtkSourceMarkAttributes, and the current line is
  highlighted. Continue, Step Over, Step Into, Step Out and Stop are available. Panels show Locals, Watch,
  Call Stack and Breakpoints.
- **Hover values** (only while the program is stopped): a GtkSourceHoverProvider pulls the expression under the
  mouse from the text. That's an identifier chain with `.`, `->`, `[i]`, and a leading `*` or `&`; if text is
  selected, the selection is used instead. The IDE runs `-var-create` on it in the selected stack frame. The
  tooltip shows `name : type = value`.
  - **Pointers are followed automatically.** The IDE also creates `*expr` and shows
    `p : Point* = 0x5555…  →  {x = 3, y = 4}`. A null pointer shows `nullptr`, and an unreadable address shows
    `<cannot access memory>`. `char*` is shown as a string.
  - Structs, arrays and containers expand in place, and their children are fetched only when expanded
    (`-var-list-children --all-values`). The variable objects are deleted when the tooltip closes.
  - While debugging, the debugger's hover takes priority over the LSP hover.
- The Debug key builds the Debug configuration first (`-g -O0`; `DEBUG=1` for Make projects).

## UI (Dev-C++ style)
- Menu bar: File, Edit, Search, View, Project, Execute, Debug, Tools. Below it, a toolbar.
- Left tabs: **Project** (the managed file tree), **Classes** (LSP documentSymbol), and **Debug** (watches).
- Centre: tabs of source editors.
- Bottom tabs: **Compiler** (parsed problems; click to jump to the line), **Compile Log**, **Debug**,
  **Find Results**, **Terminal** (VTE).
- Keys: F9 compile, F10 run, F11 compile & run, F12 rebuild all, F5 debug, F6 continue, F7 step over,
  F8 step into, Shift+F8 step out. Ctrl+Space shows completion, and Ctrl+click goes to the definition.

## Layout
```
CMakeLists.txt, data/packages.json, templates/{cpp-console,cpp-static,cpp-shared,c-console,web-ts}/
src/app/        Application, MainWindow (menus/toolbar/docks), Toolchain
src/editor/     EditorTab (GtkSourceView), ProjectTree, FindBar, HoverRouter (sends hover to the debugger or LSP)
src/project/    Project (.kevc load/save), ManagedBlock (marker splice), CMakeGenerator, MakeGenerator,
                CompileDbWriter, SourceWatcher, NewProjectWizard
src/packages/   PackageCatalog, TargetDetector, PkgConfig
src/ui/         ProjectOptionsDialog (General/Compiler/Linker/Packages)
src/build/      BuildRunner (cmake/make through Gio::Subprocess), GccDiagnosticParser
src/lsp/        JsonRpc, LspClient, LspManager; src/completion/ ICompletionSource, LspCompletionSource,
                SourceCompletionBridge (GObject that implements GtkSourceCompletionProvider)
src/debug/      MiParser, GdbMiSession, DebugController, ExpressionAtCursor, DebugHoverPopover, Locals/Watch/
                Stack/Breakpoint panels
src/devserver/  DevServerProcess; tools/devserver/devserver.mjs; tools/package.json (typescript, ts-language-server)
src/terminal/   TerminalPane (VTE)
tests/          GoogleTest unit tests
samples/        cpp-fmt (CMake + fmt), c-math (Make + -lm), ts-web, debug-me (pointers/structs/vector)
```

## Dev server (unchanged from the earlier plan)
`node tools/devserver/devserver.mjs --root <dir> --port 5465` binds to 127.0.0.1 only. It converts `.ts` and
extensionless imports with `ts.transpileModule`, serves everything else statically, and rejects paths outside the
project root. It pushes reloads over SSE at `/__kevc/reload`, with a script injected into `.html` responses. If
the port is busy (`EADDRINUSE`), it exits with code 2 and the status bar shows "Port 5465 in use". Bare npm
imports are not supported in this pass.

## Milestones (each one builds and runs)
1. `git init` and `.gitignore`. CMake skeleton, the toolchain check, and the Dev-C++ style window shell
   (menus, toolbar, docks).
2. Project model, the managed-block generators for CMake and Make, the compile_commands writer, the New Project
   wizard, the editor, the project tree, and automatic source adding.
3. Build and run (F9/F10/F11/F12), the compile log, the problems list, and the terminal.
4. Project Options dialog: compiler and linker settings, plus FetchContent catalog, custom Git and pkg-config.
5. LSP IntelliSense: completion, hover, go-to-definition, diagnostics, and the Classes tab.
6. Debugger: GDB/MI session, breakpoints, stepping, panels, then the hover tooltips that follow pointers.
7. TS dev server and the web-ts template.

Out of scope for the first pass: git integration, plugins, themes beyond the default style scheme, packaging,
Windows/macOS, bare npm imports, our own completion engine, and memory/disassembly views.

## Verification
- `cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build`. Unit tests cover the
  ManagedBlock splice, which must keep user text outside the markers byte-for-byte, the CMake/Make generators
  (golden files), GccDiagnosticParser, MiParser (recorded gdb output), ExpressionAtCursor, and JsonRpc framing.
- **C++:** create a new C++ Console project. Running `touch src/extra.cpp` from the terminal should add it to the
  managed block within about 1 s, and deleting it should remove it. Add fmt from the catalog. F11 should build and
  run it, and the Compile Log should show g++ being used.
- **C:** create a new C Console project and add `m` in the Linker tab. A new `.c` file should show up in `SRCS`,
  `make` should succeed, and clangd completion should work through the IDE-written compile_commands.json.
- **IntelliSense:** `std::vec` then Ctrl+Space suggests `vector`, Ctrl+click opens the gcc 13 header, and a
  deliberate error appears as an underline and in the Compiler tab.
- **Debugger** (`samples/debug-me`): set a breakpoint and press F5. Hovering an `int` shows its value. Hovering a
  `Point*` shows the address followed by `{x=…, y=…}`, a null pointer shows `nullptr`, and a `std::vector<int>`
  expands to its elements. Stepping keys work, and program output appears in the terminal.
- **TS:** check that `curl -s localhost:5465/main.ts` returns JS and that path traversal returns 403/404,
  that `ss -ltn` shows the server listening on 127.0.0.1 only, that saving a file reloads the browser, and that
  a busy port shows the status message.
- Hide gcc from PATH and check the IDE refuses to start with a clear dialog.
