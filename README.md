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

`kev` goes in `~/.local/bin`, which terminals only search if the login put it on PATH. Debian-based
desktops (Devuan, MX, LCOS…) don't read `~/.profile` when you log in, so there the installer offers
to link `kev` into `/usr/local/bin` (with sudo), and every terminal finds it. Kev-C++'s own
Terminal tab always finds it.

## Updates

Every push to `master` builds Kev-C++ on GitHub, runs the tests and publishes a
[release](https://github.com/kevin192291/Kev-Cpp/releases) (`.github/workflows/release.yml`). Versions are
the commit's date and time in UTC plus its short hash, such as `2026.10.31.1959-3fa834c`; Help → About
shows yours.

**Help → Check for Updates** compares your version with the latest release. If there's a newer one, it
downloads the package and runs its installer in the Run tab, then offers to restart. The installer
replaces the program, its bundled tools and the menu entry, and installs any missing packages with apt
(it asks for your sudo password there). To install a release by hand, unpack it and run
`./install.sh` (`--prefix DIR` to choose where; the default is `~/.local`).

## Features

- **git status in the Project tab**: a file with changes that aren't committed has an amber dot
  after its name (a green plus if it's new, a red ! for a merge conflict), and so does each folder
  holding one. They update when you save, and when you commit or stage in any terminal. Kev-C++ only
  shows this; you commit and push with git yourself.
- **Welcome tab**: the projects and folders you opened lately, to the left of the main shortcuts.
  Click one to open it, and click its pin to keep it in the Pinned list at the top. The tab comes
  back when you close the last file.
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
- **Toolbars and panels to suit the project**: View → Toolbars shows or hides each group of toolbar
  buttons, and View → Panels (or Close Panel on a tab's ⋮ menu) each panel, separately for each kind of
  project (C++, C, web…). The web toolbar and web panels only show for web projects by default.
- **GTK designer**: GtkBuilder `.ui` files (GTK 4) open in a design view with a live preview of the
  window. Click a widget to select it; the outline lists every object and the property list shows the
  selected one's properties by class, with the values written in the file in bold. Edits can be undone
  (Ctrl+Z, Ctrl+Shift+Z), and saving changes only the lines that were edited. The XML tab shows the file
  as text, opened at the selected object. Classes Kev-C++ doesn't have (your own widgets, libadwaita's)
  show as labelled placeholders, and nothing in the preview reacts to clicks or keys.
  Widgets come from the palette on the left: drag one into the design (an orange line or area shows
  where it will go: between a box's children, a free grid cell, a header bar's start or end, an empty
  window) or click it to add it to the selection. Dragging a widget in the design moves it. Right-click
  for cut, copy, paste, delete, Wrap in Box/Grid/Scrolled Window/Frame and Move Up/Down (Delete and
  Ctrl+C/X/V work too). The Signals tab names a handler for each signal, and a widget in a grid shows
  its cell (column, row, spans) with its properties.
- **Code for your designs**: the designer's Code menu writes a gtkmm class (`main_window.ui.h/.cpp`:
  a `MainWindowUi` base with a member for each widget with an id and a virtual method for each
  handler) or C functions (`main_window-ui.h/.c`: a struct of widget pointers and
  `main_window_ui_new()`). These are rewritten every time you save the design, and the project gets
  the gtkmm-4.0 or gtk4 package. Your code goes in `main_window.h/.cpp` (or `main_window.c`), which
  Kev-C++ writes once and never touches again, except that the → button next to a handler in the Signals tab adds
  the handler if it's missing and opens it. Code → Export Blueprint writes a `.blp` copy of the design.
  New Project has **GTK 4 Application (gtkmm)** and **GTK 4 Application (C)**: a window made in the
  designer that builds and runs with F11.

### Web projects (SvelteKit, Svelte, Vite)

A folder with a `package.json` opens as a web project; Kev-C++ reads its dependencies to tell SvelteKit,
Svelte and Vite apps apart (the title bar says which) and which package manager it uses.

- **SvelteKit App** (File → New Project) runs the official `sv create` (Svelte 5, TypeScript) and opens
  the result. **Install Dependencies** and **Project → Scripts** run `npm install` and any package.json
  script.
- **Run (F10)** starts the project's own Vite on 127.0.0.1, on the first free port from 5173, and opens
  the browser. **Compile (F9)** runs `svelte-kit sync` and `svelte-check`, with errors in the Compiler tab;
  **Rebuild (F12)** runs the `build` script.
- **Svelte IntelliSense**: completion, hover, go to definition and diagnostics in `.svelte` files
  (svelte-language-server, bundled), and `.ts` files understand Svelte components
  (typescript-svelte-plugin). `.svelte` files are highlighted.
- **Routes** tab: every route under `src/routes` with its files, what each exports (load and the keys it
  returns, form actions, HTTP verbs), where a page's `data` comes from through its layouts, and remote
  functions. ↗ opens a route in the browser (asking for its parameters); + creates a route.
- **Environment** panel: every `$env` variable, public or private, where it's defined (`.env`,
  `.env.local`, …) and used, with warnings for a private variable in code that reaches the browser, a
  static one no `.env` defines, and the wrong module for its prefix.
- **Debugging (F5)**: breakpoints in `+page.server.ts`, endpoints, hooks and other server code, and in
  browser code when Chrome, Chromium, Brave or Edge is installed (a separate browser window with its own
  profile opens). Stepping, call stack, locals, watches, hover and a console, through Microsoft's js-debug
  (the build downloads a pinned, hash-checked release).
- **Tests** panel: runs Vitest or Playwright, lists each test's result by file, re-runs failed tests or
  one test, and jumps to where a test failed.
- **Traces** panel: while Kev-C++ runs a SvelteKit app it collects the app's OpenTelemetry spans
  (`OTEL_EXPORTER_OTLP_ENDPOINT` points at a local collector) and draws each request as a waterfall of
  hooks, loads and fetches. The panel explains the three steps to turn SvelteKit's tracing on.
- **Dependencies** panel: outdated packages and known vulnerabilities (`npm outdated`, `npm audit`).

### Settings

`~/.config/kev-cpp/settings.json` holds what View → Toolbars/Panels remember, plus a few options:
`/web/browser` (a command to open pages with instead of the default browser), `/web/chrome` (the
Chromium-based browser for debugging), `/web/vitePort` (the first port to try, 5173), and
`/tools/<name>` (the path of a tool, e.g. `/tools/arm-none-eabi-gcc`).

## Keyboard shortcuts

| Key | Action | Key | Action |
|---|---|---|---|
| F9 | Compile (web: type-check / svelte-check) | F5 | Start debugging |
| F10 | Run (web: serve or Vite + open browser) | F6 | Step over |
| F11 | Compile & run | F7 | Step into |
| F12 | Rebuild all | Shift+F7 | Step out |
| | | F8 | Continue |
| Ctrl+F2 | Stop build or program | Shift+F5 | Stop debugging |
| Alt+P | Project Options | Ctrl+F5 | Toggle breakpoint |
| Ctrl+Space | Completion | Ctrl+F12 | Go to definition |

Logs from the language servers are in `~/.cache/kev-cpp/logs/`.
