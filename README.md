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

- **Splash screen**: Kev-C++ Pro starts with a Dev-C++-style splash (the logo, the name and the
  version in the corner) over the main window for two seconds; a click or a key closes it sooner.
  `/startup/splash: false` in settings turns it off.
- **Search**: the field in the middle of the menu bar (**Ctrl+P**) finds anything in the open project.
  Type and a list drops down with the files whose names match (letters in order: `mwc` finds
  `MainWindow.cpp`) and the first places the text appears; click one, or use Up/Down and Enter. Its
  last row, or **Ctrl+Shift+F**, shows every place in the **Search** tab beside the Project tab, grouped
  by file, with switches for match case, whole words and regular expressions and a "files to include"
  field (`*.cpp, src/, !tests/`). Files open in an editor are searched as you see them, unsaved
  changes included. The Project tab has a field of its own that lists matching file names in place of
  the tree, the Routes tab one that filters its routes, and the Ctrl+F bar a button that searches
  every file for what's in it.
- **Files in the Project tab**: right-click a file or folder to open, rename (F2) or delete it
  (Delete; it goes to the trash), make a new file or folder in it, or copy its path. Drag a file or
  folder onto another folder to move it there (drop it on a file to use that file's folder, or on the
  empty space below the list for the project folder); a folder you hold it over opens. Files open in
  an editor follow a rename or move, unsaved changes included, and C/C++ build files are updated.
  The tree follows the folders on disk: files made, renamed or deleted anywhere (File → New Source
  File, a terminal, git) show at once.
- **git status in the Project tab**: a file with changes that aren't committed has an amber dot
  after its name (a green plus if it's new, a red ! for a merge conflict), and so does each folder
  holding one. They update when you save, and when you commit or stage in any terminal. Kev-C++ only
  shows this; you commit and push with git yourself.
- **Welcome tab**: the projects and folders you opened lately, to the left of the main shortcuts.
  Click one to open it, and click its pin to keep it in the Pinned list at the top. The tab comes
  back when you close the last file, and with File → Close Project (Close Folder for a plain folder),
  which asks about unsaved files and stops the project's build, program, debugger and dev server.
- **Projects**: File → New Project creates C++ (console, static or shared library), C console or
  TypeScript web projects. Each has a `<name>.kevc` project file, which also keeps how the project
  looks (toolbars and panels shown, Debug or Release), so it opens the same for everyone.
- **Existing CMake projects**: opening one asks how to build it. **Use Its Own CMakeLists.txt** builds, runs and
  debugs it with its own CMakeLists.txt, which Kev-C++ never edits (it only adds a `.kevc`;
  Project → Program to Run and Debug picks among its executables). **Manage Sources** also keeps its
  source list in a marked block (below). Kev-C++ itself opens this way, so it can be built and
  debugged in Kev-C++ (`Kev-Cpp.kevc`). Its windows are moving to `.ui` files that Kev-C++'s own
  designer edits; the splash (`src/ui/SplashWindow.ui`) is the first.
- **Managed build files**: Kev-C++ owns one marked block in `CMakeLists.txt` / `Makefile` and keeps it
  in sync. New, deleted and renamed source files are added and removed automatically. Everything
  outside the block is yours and is never touched.
- **Project Options** (Alt+P): language standard, warnings, optimization, defines, include and library
  directories, libraries and linker flags.
- **Packages** (Project → Packages): add libraries from a catalog (fmt, spdlog, nlohmann_json, CLI11,
  magic_enum, glm, googletest, Catch2, glfw, raylib, SDL3) or any Git repository via CMake
  FetchContent ("Detect Targets" finds what to link), or system libraries via pkg-config.
- **IntelliSense**: completion, hover, live diagnostics and a symbol list, from clangd (C/C++) and
  typescript-language-server (JS/TS). Right-click a name for **Go to Definition** (also Ctrl+click /
  Ctrl+F12), Go to Implementation (from an interface or virtual method to the code that implements
  it), Go to Type Definition, Go to Declaration (C/C++) and Find All References; with several answers
  a list opens at the cursor. In TypeScript, Go to Definition of something from a library goes on to
  its JavaScript source instead of stopping at the `.d.ts` declarations.
- **C/C++ layout as you type**: a `{` after a class, function or `if (…)` moves to a line of its own;
  `public:`, `private:` and `protected:` snap to the class's own indentation, members one level in;
  the `}` that closes a class, struct, union or enum gets its `;`.
- **Auto Format** (Ctrl+I, in the Edit menu and the editor's right-click menu): tidies the file, or only
  the selected lines. C and C++ go through clangd, with the project's `.clang-format` when it has one
  and otherwise in the layout above (lines up to 120 columns, short member functions inside a class on
  one line). Web projects use their own prettier when it's installed, with its config; otherwise the
  TypeScript or Svelte language server formats. It's one undo step, and breakpoints stay on their lines.
- **Toggle Comment** (Ctrl+/, in the Edit menu and the editor's right-click menu): comments out the
  line the cursor is on, or puts one block comment around the selected lines (`/*` and `*/` on lines of
  their own); pressing it again takes the comment off. Each language gets its own comments: `//` and
  `/* */` in C, C++, JavaScript and TypeScript, `/* */` in CSS, `<!-- -->` in HTML, `#` in Python, CMake
  and shell scripts, and inside HTML and Svelte files the `<script>` and `<style>` parts comment as
  JavaScript and CSS. Selected lines that already hold a `*/` get a `//` each instead, since a block
  comment would end at it.
- **Build & run**: errors and warnings are listed in the Compiler tab (double-click to jump); programs run in
  the Run tab; the Terminal tab is a shell in the project folder.
- **Slate colours**: the dark slate palette of Embarcadero Dev-C++ 6, for the window and the editor,
  whatever the desktop theme is.
- **Dev-C++-style toolbar**: the classic Dev-C++ toolbar icons (New, Open, Save, Close, Undo, Find,
  Replace, Go to Line, Compile, Run, Rebuild, Debug, Toggle Breakpoint, Project Options) and pixel icons
  in the same style for the rest (see [docs/ICONS.md](docs/ICONS.md)). Each
  button that starts something (Compile, Run, Compile & Run, Rebuild All, Clean, Debug, the dev server)
  turns into a Stop button while it runs; Continue turns into Pause while the debugged program runs.
- **Movable panels**: every bottom panel can be docked at the bottom, docked on the right, or opened in
  its own window. Drag its tab, or use the ⋮ menu on the tab. Closing a panel window docks its panels
  back at the bottom. The layout is remembered (`~/.config/kev-cpp/layout.json`); View → Reset Panel
  Layout puts everything back.
- **Debugger** (gdb): click a line number to set a breakpoint; stepping, call stack, locals, watches and a gdb
  console. Hover a variable while paused to see its value; pointers are followed automatically
  (`p = 0x7ffe… → {x = 3, y = 4}`). Breakpoints and watches are remembered for each project
  (`~/.local/state/kev-cpp/projects.json`). Docked on the right, the Debug panel can be dragged narrow:
  its Locals, Call Stack, Breakpoints and GDB Console tabs then scroll left and right.
- **Four kinds of breakpoint**, for C/C++ and JavaScript alike. Right-click a line number (or use the
  Debug menu, the editor's right-click menu or the buttons on the Breakpoints tab):
  - **Breakpoint**: stops on the line (a click on the line number does the same).
  - **IF Breakpoint**: stops only when a condition is true, e.g. `i == 41`.
  - **Live Breakpoint**: never stops. Each time the line runs, the values you name (or every local
    variable) appear after the line and on the Breakpoints tab, with a count:
    `i = 41, total = 1523.5   ×42`.
  - **Break When Value Changes**: stops as soon as a variable changes, and says from what to what
    (`player.score changed from 15 to 20`). In JavaScript it works on properties (`player.score`,
    `items[0]`), not plain variables.
  The Breakpoints tab lists them all with their condition or values, a pencil to change them and a
  button to remove them. A gdb without Python support (some ARM toolchains) still gets live
  breakpoints; they pause for a moment each time instead.
- **Find and replace**: Ctrl+F finds in the current file; Ctrl+R (or the ⇄ button on the find bar)
  adds a Replace row (Replace, Replace All; one Ctrl+Z undoes it). The Search tab's Replace All
  replaces every match in the list across files, after asking; files open with unsaved changes
  change in their editor. With no project open, building, running and debugging are greyed out.
- **TypeScript dev server**: serves the project on http://127.0.0.1:5465/ (this computer only),
  converts `.ts` to JavaScript on request, and reloads the browser on save. Compile (F9) type-checks
  with `tsc --noEmit`.
- **Toolbars and panels to suit the project**: View → Toolbars shows or hides each group of toolbar
  buttons, and View → Panels (or Close Panel on a tab's ⋮ menu) each panel, separately for each kind of
  project (C++, C, web…). What a project can't use isn't shown at all: a C or C++ project has no web
  toolbar, web panels (Dev Server, Environment, Tests, Traces, Dependencies) or npm menu items; a web
  project has no Debug/Release choice, Clean, Project Options or Packages, and its build buttons and
  Execute menu say what they do there (Check Types, Run the dev server, Production Build). Environment
  and Traces only show for SvelteKit apps, Routes too.
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
  script. npm, pnpm, yarn and bun projects all work (pnpm's `.bin` shims included), and Kev-C++ finds
  nvm's (or fnm's, Volta's…) Node and pnpm even when started from the menu or a file manager.
- **Packages** (Project → Packages, or the toolbar): every dependency with the version package.json
  asks for, what's installed and the newest one (Check for Newer Versions; click one to use it), a
  **Lock** box (exactly this version) and the update rule in plain words: bug fixes only (`~`), new
  features and fixes (`^`), any newer (`>=`), any (`*`). Add and remove packages. **Package source**:
  your company's npm server for all packages or per `@scope`, saved in the project's `.npmrc`
  (Log In… runs `npm login`; tokens stay in your own `~/.npmrc`). Nothing changes until Save;
  package.json keeps its formatting.
- **Run (F10)** starts the project's own Vite on 127.0.0.1, on the first free port from 5173, and opens
  the browser. **Compile (F9)** runs `svelte-kit sync` and `svelte-check`, with errors in the Compiler tab;
  **Rebuild (F12)** runs the `build` script.
- **Svelte IntelliSense**: completion, hover, go to definition and diagnostics in `.svelte` files
  (svelte-language-server, bundled), and `.ts` files understand Svelte components
  (typescript-svelte-plugin). `.svelte` files are highlighted.
- **Routes** tab: every route under `src/routes` with its files, what each exports (load and the keys it
  returns, form actions, HTTP verbs), where a page's `data` comes from through its layouts, and remote
  functions. ↗ opens a route in the browser (asking for its parameters); + creates a route. The field
  at the top keeps the routes whose URL, files or exports hold every word you type (`blog load`).
- **Environment** panel: every `$env` variable, public or private, where it's defined (`.env`,
  `.env.local`, …) and used, with warnings for a private variable in code that reaches the browser, a
  static one no `.env` defines, and the wrong module for its prefix.
- **Debugging (F5)**: breakpoints in `+page.server.ts`, endpoints, hooks and other server code, and in
  browser code when Chrome, Chromium, Brave or Edge is installed (a separate browser window with its own
  profile opens). Stepping, call stack, locals, watches, hover and a console, through Microsoft's js-debug
  (the build downloads a pinned, hash-checked release). IF, live and value-change breakpoints work
  here too (see the Debugger above).
- **Tests** panel: runs Vitest or Playwright, lists each test's result by file, re-runs failed tests or
  one test, and jumps to where a test failed.
- **Traces** panel: while Kev-C++ runs a SvelteKit app it collects the app's OpenTelemetry spans
  (`OTEL_EXPORTER_OTLP_ENDPOINT` points at a local collector) and draws each request as a waterfall of
  hooks, loads and fetches. The panel explains the three steps to turn SvelteKit's tracing on.
- **Dependencies** panel: outdated packages and known vulnerabilities (`npm outdated`, `npm audit`).
- **Running Servers** (SvelteKit apps: Tools menu, and the globe with a red square on the web toolbar):
  every port your programs listen on, with the program (`vite dev`, the folder it runs in, its PID) and
  whether it answers: Yes, Yes with errors (a 500), Not a web server, No: stuck?, or No: suspended (a
  server stopped with Ctrl+Z still holds its port). **Kill** asks the program to stop and forces it
  after 3 seconds if it doesn't; the message says whether the port is free. Kev-C++'s own helpers (the
  traces collector, js-debug) say what they are.

### Settings

`~/.config/kev-cpp/settings.json` holds what View → Toolbars/Panels remember, plus a few options:
`/web/browser` (a command to open pages with instead of the default browser), `/web/chrome` (the
Chromium-based browser for debugging), `/web/vitePort` (the first port to try, 5173),
`/tools/<name>` (the path of a tool, e.g. `/tools/arm-none-eabi-gcc`) and `/startup/splash` (false: no
splash screen). With a project open, View → Toolbars/Panels and Debug/Release are saved with the
project instead.

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
| Ctrl+P | Search files and text | Ctrl+Shift+F | Find in all files (Search tab) |
| Ctrl+F | Find in the current file | F2 / Delete | Rename / delete in the Project tab |
| Ctrl+R | Replace in the current file | Right-click | Go to Definition / Implementation / References |
| Ctrl+I | Auto Format (file or selected lines) | Right-click a line number | Breakpoint, IF, Live, Break When Value Changes |
| Ctrl+/ | Toggle comment (the line, or a block around selected lines) | | |

Logs from the language servers are in `~/.cache/kev-cpp/logs/`.
