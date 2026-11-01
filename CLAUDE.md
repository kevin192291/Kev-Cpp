# Kev-C++ — notes for Claude (and humans)

A Dev-C++-style IDE for Linux (GTK 4 / gtkmm 4.10, C++20). C++ projects build with **CMake + g++**,
C projects with **Make + gcc**, TypeScript projects are served by a Node dev server on **port 5465**.
See README.md for features and keys; docs/PLAN.md is the original plan the code was built from.
The project is called Kev-C++ (Kev-Cpp in file and repo names), never Dev-K++.

## Build & test

```sh
cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build   # 222 tests
./install.sh    # Release build in build/release → ~/.local, updates the copy in the applications menu
```

`install.sh` patches the installed launcher's Exec to prepend node's directory when node isn't in a
standard bin dir (nvm): menu launches get the session PATH, where `node` is Ubuntu's v18, which is too
old for typescript-language-server 5 and Vite 7. A plain `cmake --install` overwrites the launcher
without that, and file managers, panels or a dev build don't use the launcher at all, so `Application`
also fixes PATH at startup (`terminalLikePath`): the newest nvm/fnm/Volta/asdf/mise/n node goes first
when PATH's node is missing or older than 20, and pnpm's and bun's folders are appended.

Tests: GoogleTest for `src/core` (headless, includes real cmake/g++/make/gcc/gdb/tsc/git runs) plus
`node --test tools/devserver/devserver.test.mjs` (registered in ctest). Everything GTK-dependent is
verified by driving the real app (see "GUI testing").

## Layout

- `src/core/` — **no GTK**; all unit-tested. Project model (`.kevc` JSON), `ManagedBlock` (the
  marker-delimited block we own inside CMakeLists.txt/Makefile), `BuildFiles` (CMake/Make generators,
  compile_commands.json for C), `BuildPlan` (commands per action/config), `Diagnostics` (gcc/ld/CMake/
  make/tsc output parser), `Packages` (catalog, pkg-config, FetchContent target detection), `Lsp`
  (framing, URIs, UTF-16 columns, markdown→Pango), `Mi` (GDB/MI parser, expression-under-cursor),
  `Breakpoints` (the breakpoint kinds, their saved form, gdb dprintf and js-debug logpoint messages,
  JavaScript watch traps), `ProcessTree` (a process's descendants from /proc),
  `Icons` (which kevc-* icon a file, LSP symbol/completion kind or gdb type gets), `RecentProjects`
  (the Welcome tab's recent and pinned folders, in settings under /recent), `GitStatus` (`git status
  --porcelain -z` → which files have uncommitted changes, for the Project tab), `Settings`
  (`~/.config/kev-cpp/settings.json`), `ToolLocator` (PATH, node_modules/.bin, `/opt/st/stm32cube*`),
  `ProjectKinds` (per-kind traits, toolbar/panel visibility per profile), `WebProject` (package.json
  flavour, package manager, Vite/svelte-check/sv commands), `SvelteKit` (routes, exports, load keys,
  data flow, `$env` report), `Dap` (js-debug launch configs, DAP replies), `TestReport` (Vitest/
  Playwright JSON), `WebReports` (OTLP spans → waterfall, npm outdated/audit), `UiDocument` (GtkBuilder
  `.ui` model that writes untouched text back verbatim, plus the designer's preview copy), `UiEdit`
  (the designer's catalog of container slots; placing, moving, wrapping, removing objects; signals),
  `UiCodegen` (gtkmm/C code and Blueprint from a design; Go to handler stubs), `FileOps` (the Project
  tab's rename/move checks, where open files go after a move), `Search` (project file listing, Go to
  File fuzzy ranking, Find in Files matching and replacing, include filters, result previews),
  `ProjectState` (breakpoints, watches and a web folder's look, per project, in
  `$XDG_STATE_HOME/kev-cpp/projects.json`), `NpmPackages` (version rules in words, text-preserving
  package.json edits, .npmrc registries), `CppStyle` (the C/C++ layout kept while typing).
- `src/app/` — `Application` (startup, toolchain check, accelerators, CSS, icon, the splash), `MainWindow`
  (everything wired together: menus, actions, build/run, debugging, IntelliSense, dev server).
- `src/editor/` EditorTab (GtkSourceView; breakpoints = source marks), CppIndenter (the
  GtkSourceIndenter for C/C++ files, using core/CppStyle), ProjectTree (with Go to File search,
  context menu, drag and drop), SymbolsPanel, and DocumentPage, the interface MainWindow saves,
  closes and moves editor-area pages through.
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
  line output; Vite, the dev server, js-debug, the OTLP collector; stopping it signals the group and
  every descendant that left it), Capture (run + collect stdout) and
  BackgroundJob (one worker thread at a time, results posted to the main loop, cancelled and joined
  before a new one starts or its owner goes; used by the searches).
- `src/build/` BuildRunner (sequential subprocesses, line streaming), CompilerPanel.
- `src/ui/` Dialogs, ProjectOptionsDialog (compiler/linker/packages), DockManager (panels docked
  bottom/right/floating or closed; layout in `~/.config/kev-cpp/layout.json`), IconWidgets (TaskButton,
  icon tab labels), QuickSearch (the menu bar's search field), SearchPanel (the left dock's Search
  tab: Find in Files), SearchMarkup (their shared highlighting), NpmPackagesDialog (a web project's
  Project → Packages), SplashWindow (start-up splash), and the web panels
  EnvPanel, TestsPanel, TracesPanel, DependenciesPanel (`src/editor/RoutesPanel` is the left-dock
  Routes tab, with a filter field).
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
  The Terminal tab puts the folder an installed kev-cpp runs from (one with `kev` beside it) first on
  its shell's PATH: Debian-based desktops don't read `~/.profile`, so `~/.local/bin` is often missing.
  It sets KEVC_TERMINAL_PATH_ADDED to that folder. Both installers' `link_kev` ignore that entry
  when checking PATH (updates run in the Run tab), and offer a `/usr/local/bin/kev` symlink.
  `KEVC_LINK_DIR` redirects the link for tests.
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
- **Breakpoint kinds** (core/Breakpoints; the user wants them for C/C++, JavaScript and later STM32):
  standard, IF (stops when a condition holds), live (never stops; each time the line runs its values
  show after the line and in the Breakpoints tab, `i = 41, total = 1523.5   ×42`), and break when a
  value changes (a watch on an expression, not on a line). Right-clicking a line number shows them
  (EditorTab's gutter popover, caught in the capture phase or the text menu opens instead); so do the
  Debug menu, the text's right-click menu and the Breakpoints tab's buttons. Marks use one category
  per kind with the Breakpoint on the mark (`kevc-breakpoint` data). Saved per project: standard ones
  as plain line numbers as before, the others as objects; value watches as `valueWatches`.
  - gdb: IF = `-break-insert -c`. Live = `-dprintf-insert` whose arguments go through Python
    functions defined at start (`gdbLivePython`, sent hex-encoded): a dprintf whose expression fails
    halts the program silently, the functions turn errors into `<message>` text. No expressions =
    `$_kevc_locals()`. dprintf output arrives as console records starting `<<kevc-live:ID>>`; they
    never reach the GDB Console. A gdb without Python (some arm-none-eabi-gdb) gets breakpoints that
    stop, read, and `-exec-continue` without reporting a stop. Breakpoints are placed only after
    gdb answers the Python command. Value watches = `-break-watch`; one that can't be placed yet (a
    local not in scope) is retried at every pause and after `watchpoint-scope`.
  - gdb's `=breakpoint-modified` records carry `script={"…"}`, a brace list of bare strings; the MI
    parser reads it as a list (it used to fail and the raw line counted as a live hit).
  - js-debug: IF = `condition`, live = `logMessage` (`<<kevc-live:ID>>{a}<<kevc>>{b}`), caught in
    output events. A live breakpoint naming nothing stays a plain breakpoint: JsDebugger reads the
    innermost scope with variables (not Global) and continues. js-debug has no data breakpoints, so a
    watched property (`player.score`, `items[0]`; plain variables can't be) becomes a getter/setter
    whose setter runs `debugger;` (`jsWatchInstall`, evaluated when paused); after each stop
    `jsWatchLastChange()` says whether it was one, and the first frame with a file is shown.
  - Live updates are drawn at most every 150 ms (`flushLiveValues`). Editor value labels are text
    view overlays that are hidden and reused, never removed: GTK 4.14's gtk_text_view_remove()
    refuses overlay children.
  - Test hooks: `KEVC_GDB_NO_PYTHON=1` takes the no-Python path; `KEVC_GDB_LOG=<file>` logs gdb's MI
    traffic both ways.
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
  `.bin` tools are resolved to their scripts and run with our node (`nodeBin`), not via PATH: npm's
  `.bin` entries are symlinks, pnpm's are `#!/bin/sh` shims whose `exec node "$basedir/…"` line names the script.
- **Toolbar groups / panels**: named groups (file, edit, build, debug, project, web) and closable
  panels, Auto/Shown/Hidden per profile in settings (`/toolbars/<id>/<profile>`, `/panels/<id>/<profile>`);
  `applyProfile()` after every open. Defaults live in `ProjectKinds.cpp` (web-only: web toolbar,
  dev-server/env/tests/traces/deps panels). On top of that, what can't apply is gone, not just off
  by default (`toolbarGroupApplies`, `panelApplies`, `usesNativeTools`/`usesWebTools`): C/C++ hide the
  web group, web panels, npm menu items and dev-server Tools items; web hides the project group,
  Debug/Release, Clean, Packages/Project Options; env/traces need SvelteKit. Such items are also left
  out of View → Toolbars/Panels. `updateMenus()` refills the View lists and the Project, Execute and
  Tools menus (web names: Check Types, Production Build) on every applyProfile.
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
- **Project tab file operations**: ProjectTree only emits rename/delete/new/move signals (context
  menu, F2/Delete, drag and drop); MainWindow does them with `core/FileOps` checks, so open documents
  follow (`DocumentPage::moveTo`; editors are detached from and re-attached to the language servers)
  and breakpoints move with them. Delete uses the trash (`g_file_trash`), with a permanent-delete
  fallback. Drags carry the row's GFileInfo, which no other program can read, so rows never leave
  the tree. GUI tests set `XDG_DATA_HOME` so test deletions don't land in the user's real trash.
- **Search is visible without knowing it's there** (the user's rule: a first-time user must find it at
  a glance, no instructions): `QuickSearch` sits in the middle of the menu bar's row (a CenterBox
  around the PopoverMenuBar, like Visual Studio's and VS Code's) and drops down file-name matches
  (fuzzy, scattered mid-word letters rejected) plus the first 8 text matches, with a last row to the
  Search tab. Ctrl+P focuses it. Ctrl+Shift+F is the left dock's Search tab, second after Project (all
  matches as you type, grouped by file, stops at 2,000; open editors searched with their unsaved
  text). The Project tab's own field lists file names in place of the tree; the Routes tab filters.
  The bottom dock's old "Find Results" placeholder is gone: build output there would bury results.
  All of them list files with `search::listFiles` (the Project tab's hiding rules) on a BackgroundJob.
  The left dock's tabs keep their names where they fit (selected, then Project, then Search:
  `fitLeftTabs`, measured on every switch and pane resize). EditorTab's scrollToCursor scrolls back
  to column 0 when the cursor and selection fit, so jumps don't hide indentation.
- **QuickSearch's drop-down** is a non-autohide popover (autohide would take the keyboard from the
  field), as GTK's suggestion entry does. It closes on Escape, a choice, another widget taking focus,
  or a press on the main window's own surface outside the field: presses in the popover also pass
  through the window's capture controllers, so the surface check matters. Row activation is deferred
  to idle because activating empties the list.
- **Per-project state**: breakpoints and watches (and, for web folders, which have no .kevc, the
  ProjectUi: toolbars/panels shown, Debug/Release) are saved to ProjectStateStore whenever they change
  and before a project closes, and restored when it opens. C/C++ projects keep their ProjectUi in the
  .kevc ("ui"). With a project open, View → Toolbars/Panels and the Debug/Release choice change the
  project's ProjectUi; with nothing open, the per-profile settings as before. GUI tests must set
  XDG_STATE_HOME (and XDG_DATA_HOME, for the trash) to scratch folders.
- **No project open**: updateToolbar() also enables/disables the win.* build, run, debug and project
  actions, so menus grey out and their keys do nothing.
- **C/C++ indenter**: a GtkSourceIndenter set on C/C++ buffers. GtkSourceView 5 asks is_trigger on key
  press, lets the input method insert the key, then calls indent() just after it (only if the IM
  inserted exactly that character). Triggers: Return, '{', '}', ':'. Decisions are in core/CppStyle
  (Allman braces, access specifiers at the class's indentation, ';' after a class body).
- **Go to…**: LspManager::locations() for definition/declaration/typeDefinition/implementation/
  references; Go to Definition that lands only in .d.ts files asks typescript-language-server's
  `_typescript.goToSourceDefinition`. Several answers show in locationsPopover_ (parented to
  editorArea_). Right-click moves the cursor (outside a selection) so the menu acts on the clicked
  word; the items are the editors' extra_menu.
- **Replace**: the find bar's Replace row uses GtkSourceSearchContext replace/replace_all; Find in
  Files' Replace All re-runs the matcher on each listed file (search::Matcher::replace, $1 groups for
  regex), writes files directly, and hands open modified editors their new text (one undo step).
- **npm Packages dialog**: package.json is edited as text (npm::setDependency/removeDependency), so
  formatting and key order stay; .npmrc keeps every line but registry ones. Newest versions and
  lookups for Add use `npm view … --json` with the dialog's registry flags, 4 at a time.
- **Build As It Is** (Project::buildFileManaged = false): no managed block, no source sync; the
  project's CMakeLists.txt builds as usual and runnableOutput() looks for `targetName` anywhere under
  the build folder. cmakeExecutables() lists add_executable() targets for the chooser. The repository
  has its own Kev-Cpp.kevc this way.
- **Splash** ("Kev-C++ Pro", the app icon, the version bottom right): Application::on_activate shows
  it once per start after presenting the main window, transient and modal so window managers centre
  it there (GTK 4 can't place windows). 640 px wide for the CF-19's crocus; closes after 2.2 s or on a
  click or key; `/startup/splash` false turns it off. GUI tests: it covers the window for ~2 s.
- **git in the Project tab** is read-only: `watchGit()` (on every folder open) asks git for the work tree
  and .git folder, watches .git for index/HEAD rewrites (a commit or add in any terminal), and
  `refreshGitStatus()` also runs after saves, source syncs, window activation and every 5 s while the
  window is active. One `git status` at a time; paths are mapped from the work tree's top to the
  tree's spelling of the project root.
- **Signals**: SIGTERM, SIGINT and SIGHUP close the window as its close button does, without asking
  about unsaved files, so every child process is stopped. js-debug starts its watchdog in a session
  of its own under Vite; while it lived Vite waited for its debugger and never exited, which is why
  ManagedProcess signals descendants too (core/ProcessTree, read before signalling).
- **Panes**: the middle column (editor + bottom dock) can't be shrunk below its minimum; GTK would
  draw it at that width anyway, cut off on the left. The docks give way.
- **Async safety**: callbacks from gdb/LSP/processes capture a `weak_ptr` token, never raw `this`
  alone (`sigc::track_obj` only protects sigc slots, not `std::function`). BackgroundJob's posts may
  capture `this`: they're dropped once the job is cancelled, and its owner's destructor cancels it.

## GUI testing (important)

- Never send synthetic input to the user's real display (:0). Run `Xephyr :5 -screen 1400x950` and
  start the app with `DISPLAY=:5 GDK_BACKEND=x11 XDG_CONFIG_HOME=<tmp> XDG_CACHE_HOME=<tmp>` so test
  runs don't touch the user's panel layout or caches. Drive it with python-xlib XTest; screenshot with
  `root.get_image`. No window manager in Xephyr: windows open at (0,0), focus follows the pointer.
- Popovers that don't grab (QuickSearch's list) misbehave without a window manager: focus follows
  the pointer onto the popup and the main window loses focus. Run `DISPLAY=:5 metacity --replace
  --sm-disable` inside Xephyr for those tests (always with DISPLAY=:5: never replace the user's WM).
  With it the window is decorated and offset, so take coordinates from a screenshot.
- Never trigger file dialogs (Ctrl+O, Browse…) in tests: GTK routes them through the desktop portal,
  so they appear on the user's real screen and block the app. Open projects via the command line.
- Don't press F10/Open in Browser on web projects in tests (it opens the user's real browser).
- Drag panel tabs with an XTest press, small motion steps and a release (see a scratch `drag.py`);
  dropping on the bare desktop is safe now that panels use their own drag (see Docking).
- The app picks its own theme, so tests look like the user's desktop without extra setup. Don't set
  `GTK_THEME` in tests: it overrides the app's choice.
- Stop test instances by their command line (the test project path), never by name (`pkill kev-cpp`,
  `pgrep -x gdb`): the user may have their own Kev-C++ or gdb running.
- JavaScript debugging tests: delete `$XDG_CACHE_HOME/kev-cpp/debug-browser-profile` before a run.
  Chromium restores the tabs of a session it didn't close, and a restored page whose Vite still runs
  hits the same breakpoints (live counts double).

## State and known gaps

All seven planned milestones are done (projects, build/run, options & packages, IntelliSense,
debugger, dev server) plus docking panels. Not done yet: the bundled TypeScript dev server (projects
without Vite) can't resolve npm packages; floating panel window positions aren't remembered (GTK 4 can't
position windows; the user doesn't mind); Kev-C++'s own windows are built in code, not .ui files, so
the GTK designer can't edit them yet.

Roadmap: docs/ROADMAP.md. Done: M0 (foundations), W1–W5 (web/SvelteKit, including server and
browser debugging, checked end to end) G1–G3 (GTK designer: model, preview, properties; palette, drag and
drop, signals; gtkmm/C code, GTK 4 app templates). Next: S1 (STM32). Each milestone gets its own plan
first.

## Conventions

C++20, 4-space indent, `kevc` namespace, member suffix `_`, comments explain *why*. New logic goes in
`src/core` with tests whenever it can be GTK-free. Commit messages end with the Co-Authored-By line.
