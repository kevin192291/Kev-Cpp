# Kev-C++ — notes for Claude (and humans)

A Dev-C++-style IDE for Linux (GTK 4 / gtkmm 4.10, C++20). C++ projects build with **CMake + g++**,
C projects with **Make + gcc**, TypeScript projects are served by a Node dev server on **port 5465**.
See README.md for features and keys; docs/PLAN.md is the original plan the code was built from.
The project is called Kev-C++ (Kev-Cpp in file and repo names), never Dev-K++.

## Build & test

```sh
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build   # 164 tests
./install.sh    # Release build in build/release → ~/.local, updates the copy in the applications menu
```

`install.sh` patches the installed launcher's Exec to prepend node's directory when node isn't in a
standard bin dir (nvm): menu launches get the session PATH, where `node` is Ubuntu's v18, which is too
old for typescript-language-server 5. A plain `cmake --install` overwrites the launcher without that.

Tests: GoogleTest for `src/core` (headless, includes real cmake/g++/make/gcc/gdb/tsc/git runs) plus
`node --test tools/devserver/devserver.test.mjs` (registered in ctest). Everything GTK-dependent is
verified by driving the real app (see "GUI testing").

## Layout

- `src/core/` — **no GTK**; all unit-tested. Project model (`.kevc` JSON), `ManagedBlock` (the
  marker-delimited block we own inside CMakeLists.txt/Makefile), `BuildFiles` (CMake/Make generators,
  compile_commands.json for C), `BuildPlan` (commands per action/config), `Diagnostics` (gcc/ld/CMake/
  make/tsc output parser), `Packages` (catalog, pkg-config, FetchContent target detection), `Lsp`
  (framing, URIs, UTF-16 columns, markdown→Pango), `Mi` (GDB/MI parser, expression-under-cursor),
  `Icons` (which kevc-* icon a file, LSP symbol/completion kind or gdb type gets), `Settings`
  (`~/.config/kev-cpp/settings.json`), `ToolLocator` (PATH, node_modules/.bin, `/opt/st/stm32cube*`),
  `ProjectKinds` (per-kind traits, toolbar/panel visibility per profile), `WebProject` (package.json
  flavour, package manager, Vite/svelte-check/sv commands), `SvelteKit` (routes, exports, load keys,
  data flow, `$env` report), `Dap` (js-debug launch configs, DAP replies), `TestReport` (Vitest/
  Playwright JSON), `WebReports` (OTLP spans → waterfall, npm outdated/audit), `UiDocument` (GtkBuilder
  `.ui` model that writes untouched text back verbatim, plus the designer's preview copy), `UiEdit`
  (the designer's catalog of container slots; placing, moving, wrapping, removing objects; signals),
  `UiCodegen` (gtkmm/C code and Blueprint from a design; Go to handler stubs).
- `src/app/` — `Application` (startup, toolchain check, accelerators, CSS, icon), `MainWindow`
  (everything wired together: menus, actions, build/run, debugging, IntelliSense, dev server).
- `src/editor/` EditorTab (GtkSourceView; breakpoints = source marks), ProjectTree, SymbolsPanel, and
  DocumentPage, the interface MainWindow saves and closes editor-area pages through.
- `src/designer/` DesignerPage: the GTK designer's page for `.ui` files (preview, outline, properties,
  signals, XML); DesignerEditing.cpp holds its palette, drag and drop, clipboard and context menu;
  DesignerCode.cpp its Code menu, regeneration on save and Go to handler;
  DesignerTypes (GType lookups, the catalog from `data/designer/catalog.json` in the GResource).
- `src/lsp/` LspClient (JSON-RPC over stdio), LspManager (documents ↔ servers), LspBridges
  (GObject completion/hover providers for GtkSourceView, written in C style).
- `src/debug/` GdbSession (MI process), DebugController (session state), DebugViews (VarNode tree,
  Debug panel, Watches), DebugHover (hover provider); for web projects DapClient (one DAP session
  over TCP), JsDebugger (js-debug server + root/child sessions) and DapViews (DapVarNode).
- `src/process/` ManagedProcess (long-running child: own process group, PR_SET_PDEATHSIG, stdin pipe,
  line output; Vite, the dev server, js-debug, the OTLP collector) and Capture (run + collect stdout).
- `src/build/` BuildRunner (sequential subprocesses, line streaming), CompilerPanel.
- `src/ui/` Dialogs, ProjectOptionsDialog (compiler/linker/packages), DockManager (panels docked
  bottom/right/floating or closed; layout in `~/.config/kev-cpp/layout.json`), IconWidgets (TaskButton,
  icon tab labels), and the web panels EnvPanel, TestsPanel, TracesPanel, DependenciesPanel
  (`src/editor/RoutesPanel` is the left-dock Routes tab).
- `src/devserver/DevServer` runs the bundled `tools/devserver/devserver.mjs` or the project's Vite.
  `tools/package.json` pins typescript 5.9.3, typescript-language-server 5.3.0, svelte-language-server
  0.18.5 and typescript-svelte-plugin 0.3.52 (`npm ci` during the build); `tools/fetch-js-debug.cmake`
  downloads js-debug 1.140.0 (SHA-256 checked) into `tools/js-debug/`; `tools/otel/collector.mjs` is the
  loopback OTLP/HTTP JSON collector. `data/language-specs/svelte.lang` is copied to the cache like the
  style scheme.
- `data/` icons (GResource-embedded), `packages.json` catalog, `.desktop` template. `data/art/` holds the
  icon sprite sheet, the C++/C/TypeScript logos (`logos/`, New Project dialog) and `make_icons.py`, which generates `data/icons/hicolor/*/actions/kevc-*.png` and
  `data/kevcpp-ui-icons.gresource.xml` (committed; see docs/ICONS.md). Only the app icon is installed
  into the system icon theme. `data/kevc-theme.css` + `data/styles/kevc-slate.xml` are the Slate colours.

## Decisions worth keeping

- **Build files**: the IDE owns only the `# >>> Kev-C++ managed` … `# <<< Kev-C++ managed` block and
  regenerates it from the `.kevc`; user text outside it is preserved byte-for-byte (tested). Sources
  are listed explicitly (no GLOB). `SourceWatcher` resyncs on file create/delete/rename (250 ms debounce).
- **Opening a folder** (`openFolder`, also `kev PATH` from a terminal): a `.kevc` opens that project;
  else CMakeLists.txt asks to adopt it; else a `package.json` makes it a TypeScript project kept in
  memory only (`ProjectSession::openWebFolder`, no `.kevc` written); else a plain folder. `data/kev`
  is a shell script installed next to kev-cpp; it runs the IDE under `setsid -f` so the terminal is freed.
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
- **Toolbar**: buttons activate the `win.*` actions (not bound to them) so `updateToolbar()` owns their
  enabled state. A `Task` records which button started the running build (`buildTask_`) or program
  (`programTask_`); that button shows Stop until it ends. Call `updateToolbar()` after anything that
  starts or ends a build, program, debug session (or changes its state) or the dev server.
- **Slate theme** (colours of Embarcadero Dev-C++ 6's dark style): `Application` switches the app to
  GTK's built-in "Default" dark theme whatever the desktop uses and layers `kevc-theme.css` on top,
  so it looks the same everywhere. The editor scheme is copied to `~/.cache/kev-cpp/styles/` at
  startup because GtkSourceView 5.12 can't read schemes from a GResource. The toolbar is a lighter
  slate than the rest so the icons' black outlines show.
- **Docking**: panel tabs are dragged with DockManager's own `Gtk::DragSource` (the content is the
  panel widget as a GTK_TYPE_WIDGET value), not GtkNotebook's detachable tabs. GTK 4.14's notebook
  drag offers the tab to other programs; if one reads it mid-drag (Cinnamon's desktop does) and the
  tab is then dropped on another notebook, GTK dereferences a cleared `detached_tab` and crashes.
  Drop targets: every dock notebook (insert before the tab under the pointer), the main window
  (`acceptFloatingDrops` → own window), and a cancelled drag (dropped outside) → own window.
- **Old GPUs**: GTK 4 needs OpenGL 3.3/GLES 3.0; without it GTK 4.16+ uses Vulkan on lavapipe (CPU),
  and XLibre 25 + Mesa crocus (Intel Ironlake, e.g. Panasonic CF-19) segfaults copying its frames
  into windows not a multiple of 128 px wide (the New Project dialog). `Application` probes
  `prepare_gl()` at startup and sets `GSK_RENDERER=cairo` if it fails (unless the user set one).
- **Web flavours** (`WebProject::detect`, never saved): `@sveltejs/kit` → SvelteKit, `svelte`+`vite` →
  Svelte, `vite` → Vite, else Plain (bundled server on 5465). Vite runs directly as `node vite.js dev`
  (one process, dies with the IDE) on the first free port from 5173 with `--strictPort --host 127.0.0.1`.
  `.bin` tools are resolved to their scripts and run with our node (`nodeBin`), not via PATH.
- **Toolbar groups / panels**: named groups (file, edit, build, debug, project, web) and closable
  panels, Auto/Shown/Hidden per profile in settings (`/toolbars/<id>/<profile>`, `/panels/<id>/<profile>`);
  `applyProfile()` after every open. Defaults live in `ProjectKinds.cpp` (web-only: web toolbar,
  dev-server/env/tests/traces/deps panels).
- **JS debugging**: js-debug's `dapDebugServer.js` on a free port; the root `pwa-node` session launches
  Vite; js-debug's `startDebugging` reverse requests each get a new TCP session; once Vite prints its
  URL a `pwa-chrome` session opens it if a Chromium-based browser is found (`/web/chrome`), else the
  normal browser opens it (server code only). Debug actions dispatch to `jsDebugger_` or gdb.
  `.svelte` breakpoints go to browser sessions only (`dap::sessionWantsBreakpoints`): in Vite's SSR
  build of a component the source map doesn't lead back, so it paused on lines that don't exist.
  GUI tests: Playwright's Chromium (`~/.cache/ms-playwright/chromium-*/chrome-linux64/chrome`) as
  `/web/chrome`; it opens on the Xephyr display, and keys go to whichever window is under the pointer.
- **Test hooks in settings**: `/web/browser` (tests point it at a script that records URLs) and
  `/web/chrome`; never let tests open the user's real browser.
- **Versions and releases**: a build's version is its commit, `YYYY.MM.DD.HHMM-<hash>` in UTC
  (`cmake/version.cmake` writes `build/generated/kevc_version.h` on every build; only
  `src/app/BuildInfo.cpp` includes it, `+changes` marks a dirty tree). Pushes to master run
  `.github/workflows/release.yml`: build + tests on ubuntu-24.04, `packaging/make-release.sh` (install
  tree + `packaging/install-release.sh` as `install.sh`), `gh release create v<version>`. Help → Check
  for Updates: curl GitHub's releases/latest, `core/Version` compares by commit time, the download and
  `install.sh --prefix <prefix of the running binary>` run in the Run tab (sudo prompts work there).
  `KEVC_PRETEND_VERSION=2026.01.01.0000-0000000` makes a build look old, to test updating.
- **GTK designer**: the `.ui` file is the only model. `UiDocument` keeps each node's source text and
  rewrites only edited nodes (new ones are indented like their neighbours); uids are unique in the
  process, and undo reparses a text snapshot, finding the selection again by its child-index path. The
  preview is GtkBuilder building `makePreview()`'s copy in-process: windows become boxes, templates
  objects, unknown classes labelled placeholders; signals, closures and event controllers are dropped.
  Its frame can't be targeted or focused, so a click layer above it picks widgets
  (`gtk_widget_pick` with NON_TARGETABLE). Mnemonics are stripped (Alt+key would otherwise fire them in
  the IDE window) and empty action groups shadow `win.*`/`app.*` for action-name buttons. Theme CSS
  under `.kevc-design-frame` restores GTK's Default look where the Slate rules would restyle the design.
  Drags carry "new:<class>" or "move:<uid>"; the page remembers which (`dragging_`) since a drop
  target only sees the data on drop. Rebuilding the property list destroys the focused field, whose
  focus-out would commit stale text: every commit returns while `syncing_` is set.
- **Designer code**: generated files (`x.ui.h/.cpp` gtkmm, `x-ui.h/.c` C) are rewritten on every save
  once they exist (that's the opt-in); the user's files are only created when missing. gtkmm windows
  come from `Gtk::Builder::get_widget_derived`, and signals connect with `g_signal_connect_data`
  (swapped) to a lambda calling the virtual handler, so any signal works without a gtkmm signal
  table. C stand-ins are `__attribute__((weak))`. TypeInfo (`designer::gtkTypes()`) asks GType for
  signal signatures and property kinds, and checks gtkmm's headers via pkg-config; the project templates use
  `ui::basicGtkTypes()`. `MainWindow::addSystemPackage` adds gtkmm-4.0/gtk4 when code is written.
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
- Drag panel tabs with an XTest press, small motion steps and a release (see a scratch `drag.py`);
  dropping on the bare desktop is safe now that panels use their own drag (see Docking).
- The app picks its own theme, so tests look like the user's desktop without extra setup. Don't set
  `GTK_THEME` in tests: it overrides the app's choice.
- Stop test instances by their command line (the test project path), never by name (`pkill kev-cpp`,
  `pgrep -x gdb`): the user may have their own Kev-C++ or gdb running.

## State and known gaps

All seven planned milestones are done (projects, build/run, options & packages, IntelliSense,
debugger, dev server) plus docking panels. Not done yet: Replace (Ctrl+R) and Find Results are
placeholders; breakpoints/watches aren't saved between sessions; the dev server can't resolve npm
packages; floating panel window positions aren't remembered (GTK 4 can't position windows).

Roadmap: docs/ROADMAP.md. Done: M0 (foundations), W1–W5 (web/SvelteKit, including server and
browser debugging, checked end to end) G1–G3 (GTK designer: model, preview, properties; palette, drag and
drop, signals; gtkmm/C code, GTK 4 app templates). Next: S1 (STM32). Each milestone gets its own plan
first.

## Conventions

C++20, 4-space indent, `kevc` namespace, member suffix `_`, comments explain *why*. New logic goes in
`src/core` with tests whenever it can be GTK-free. Commit messages end with the Co-Authored-By line.
