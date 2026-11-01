# Kev-C++ Pro roadmap

> The plan for Kev-C++'s next set of features, written in October 2026 after the original plan
> (docs/PLAN.md) was finished. Each milestone gets its own detailed plan when work on it starts. CLAUDE.md
> describes the code as it is now.

## Context

Kev-C++ is meant to become a pro-grade IDE for four kinds of work: JavaScript/TypeScript with SvelteKit,
C/C++, STM32 firmware, and Linux GTK desktop apps. The roadmap rests on two things. The first is research
into what developers in those areas ask for and what no IDE does well. The second is two features the user
asked for by name:
- a drag-and-drop GTK designer;
- an STM32 toolbar that flashes through an ST-Link V2/V3. It can be shown or hidden, and it appears on its
  own for STM32 projects.

Decisions from the user:
- **Premium** means pro-grade features, **all free**. There are no licence keys and no feature gating.
- **Order:** a short Foundations milestone, then the **SvelteKit track**, then STM32, the GTK designer and
  general C/C++ features. (On 2026-10-31 the user moved the GTK designer ahead of STM32.)
- **GTK designer:** **native, inside Kev-C++**, generating **gtkmm C++, plain C and Blueprint** code.

## What the research found

These gaps aren't covered well by any IDE today, so they are where Kev-C++ can stand out.

**SvelteKit**
- **A live route map.** It would show groups, `[param]` segments, `+page`/`+layout`/`+server` files, load
  functions, form actions, and which layouts a route inherits. VS Code extensions only show the file tree.
- **Load → props navigation.** It would take you from `data.user` in a page to the `load` that returns it,
  across layouts.
- **A built-in trace waterfall.** SvelteKit 2.31+ emits OpenTelemetry spans, but viewing them today needs
  an external collector such as Jaeger.
- **One-click full-stack debugging** of server and browser code. People struggle to set this up even in
  VS Code.

**STM32**
- **Live watch and plotting that work with any probe backend.** CLion's and ST's versions only work with
  particular GDB servers.
- **A linker-map treemap with a per-build diff**, showing what grew.
- **A HardFault decoder.**
- **FreeRTOS-aware debugging** that adds the helper symbol OpenOCD needs, instead of failing with a cryptic
  error.

**GTK**
- **No GTK 4 designer generates C or C++ code.**
  - Glade supports GTK 3 only and is unmaintained.
  - Cambalache 1.0 (March 2026) edits `.ui` and Blueprint files but writes no code.
  - Workbench has no C++ support.
  - gtkmm has no signal auto-connect.

**C/C++**
- **A built-in compile-time profiler.**
- **Coverage tied to a test explorer.**
- **Clickable sanitizer reports.** The 2026 ISO C++ survey names "sanitizer friction" as a pain point.

### On the development machine

These facts shape the STM32 track.
- **STM32CubeIDE** 1.18.1 and 1.19.0 are installed in `/opt/st`. They bundle:
  - arm-none-eabi-gcc/gdb 13.3;
  - `ST-LINK_gdbserver` and `STM32_Programmer_CLI`;
  - ST's OpenOCD;
  - the `STMicroelectronics_CMSIS_SVD/*.svd` register descriptions.
- **Other ST tools:**
  - STM32CubeMX is in `~/STM32CubeMX`.
  - `st-flash` and `st-util` are on PATH.
  - The ST-Link udev rules (`49-stlinkv*.rules`) are installed.
- **The user's STM32 projects:**
  - They are STM32CubeIDE (Eclipse `.cproject`) projects in `~/STM32CubeIDE/workspace_eren/*`, plus Keil
    tutorials in `~/source/STM32_tutorials`.
  - **None of them use CMake.**
  - `ProjectSession::adoptCMake`'s regex doesn't match CubeMX's `add_executable(${CMAKE_PROJECT_NAME})`.
- **Libraries:**
  - The GTK runtime is 4.14.5, but the gtkmm headers are 4.10.
  - libxml2 2.9.14 is available.
  - GtkSourceView has `blueprint.lang` but no Svelte highlighting.

## Design decisions

- **D1 Web flavours.** Keep `ProjectKind::WebTs` and detect a flavour from package.json. The flavour isn't
  saved, so in-memory `package.json` folders stay unmodified.
  - **Flavour rules:**
    - `@sveltejs/kit` → SvelteKit
    - `svelte` + `vite` → Svelte
    - `vite` → Vite
    - anything else → Plain
  - **Package manager:** taken from the `packageManager` field or the lockfile (npm, pnpm, yarn, bun).
  - **Run:** Plain keeps the bundled dev server on port 5465; the Vite flavours run the project's own Vite.
- **D2 STM32 is a new kind,** `ProjectKind::Stm32`. Its `.kevc` gets a `"stm32"` section holding the ioc,
  preset, probe serial, backend and svd.
  - Build, run (which means flash), debug, LSP and file extensions all differ from desktop C/C++. A separate
    kind lets the compiler's `-Wswitch` find every place that needs a change.
  - The `.ioc` file's `TargetToolchain` decides how a project is imported:

    | TargetToolchain | Import |
    |---|---|
    | CMake | Use CubeMX's presets as they are. |
    | STM32CubeIDE | Generate a CMake build from `.cproject`, leaving `.cproject` untouched. |
    | Makefile | Build with `make GCC_PATH=…`. |
    | MDK-ARM / EWARM | Explain how to regenerate the project as CMake in CubeMX. |

- **D3 Flash and debug backends** are tried in this order:
  1. the user's choice;
  2. the STM32CubeCLT or STM32CubeIDE tools, newest version first;
  3. OpenOCD, using the system copy if there is one, otherwise the CubeIDE bundle;
  4. probe-rs;
  5. st-flash and st-util.

  Probes are found through sysfs, VID 0483, and always selected by serial number:

  | PIDs | Probe |
  |---|---|
  | 3748 | V2 |
  | 374b, 374a, 3752 | V2-1 |
  | 374e, 374f, 3753, 3754 | V3 |
  | 3757 | V3PWR |

  374d is the V3 USB bootloader and is skipped.
- **D4 Two debug engines behind one `DebugBackend` interface.**
  - **gdb/MI** stays for native and STM32 debugging. `DebugController` gets a launch spec that is either
    local or remote.
  - **A DAP client** is used only for Node and the browser (Microsoft's js-debug), and later for probe-rs.
    DAP uses the same Content-Length framing as LSP, so it reuses `lsp::MessageFramer`.
- **D5 The designer previews in-process on a sanitised copy of the `.ui`.**
  - **Why in-process:** GTK 4 can't embed another process's window on X11.
  - **The sanitised copy:**
    - Signals are removed.
    - Window and dialog toplevels become framed children.
    - Dummy `app` and `win` action groups stop a previewed `win.save` from firing the IDE's own action.
  - **Selection:** a capture overlay handles it.
  - **`.ui` XML is the master format.** Untouched nodes are written back byte-identical.
- **D6 Drag and drop maps to XML through a slot model** in `src/core`, driven by
  `data/designer/catalog.json`. The model knows each container's slots:
  - a single child, or ordered children;
  - typed children, such as HeaderBar `start`/`end`;
  - named properties, such as Paned `start-child`/`end-child`;
  - stack and notebook pages;
  - grid cells.

  A drop resolves to (parent, slot, index).
- **D7 Generated code never overwrites the user's code.** This is the generation-gap pattern. (As built
  in G3.)
  - **Generated files** are wholly owned and rewritten whenever the `.ui` is saved, once the user has
    written code for the design the first time (Code menu): `name.ui.h/.cpp` or `name-ui.h/.c`.
  - **User files** are created once and never rewritten. "Go to handler" only adds a missing stub.
  - **gtkmm:** a base class with typed widget members and `virtual` handlers. The handlers are connected
    with `g_signal_connect_data` to a lambda that calls the virtual method, so any signal works
    without a table of gtkmm signal methods; handlers take no arguments. When a signal is removed,
    the stale `override` turns into a compile error instead of deleted code.
  - **C:** a struct of widget pointers, with the handlers connected by `g_signal_connect`. Weak empty
    stand-ins let a project link before its handlers are written. (This replaced the planned
    `GtkBuilderCScope`, which needs every handler to exist.)
  - **Blueprint:** exported on request (`name.blp`), written from the model and never read back.
  - The `.ui` is embedded as a string (without its `<signal>`s), so it works with both CMake and Make
    projects.
  - `ManagedBlock` stays for build files only. Its markers are `#` comments and it allows one block per file.
- **D8 Toolbar groups** each have Auto, Shown or Hidden visibility, remembered per project type. Hiding the
  STM32 toolbar in one STM32 project hides it for STM32 projects only.
- **D9 Testability without real hardware or browsers.** Tool paths, the browser, Chrome and the sysfs root
  can all be set in settings. Tests then use recording scripts and fake device trees.

## Milestones (each builds, runs and ships on its own)

**Status (2026-11-01):** M0, W1–W5 and G1–G3 are done. G4 (Kev-C++'s own windows in its designer)
is under way; S1 follows it.

**Also done along the way** (not milestones):
- **Versions and updates:** a build's version is its commit's UTC date and time plus its hash. Every
  push to master builds, tests and publishes a GitHub release, and Help → Check for Updates downloads and
  installs the newest one.
- **Old GPUs:** without OpenGL 3.3, Kev-C++ switches GTK to its cairo renderer (XLibre + Mesa crocus
  crashed otherwise).
- **`kev PATH`** opens a project or folder from a terminal.
- **Editor pages:** a `DocumentPage` interface, so MainWindow saves and closes text editors and the
  designer the same way.

### Foundations

**M0 Foundations** (done)
- **Settings:** `src/core/Settings` stores them in `~/.config/kev-cpp/settings.json`. `.kevc` files keep
  keys they don't know.
- **Tool discovery:** `src/core/ToolLocator` searches:
  - PATH;
  - `node_modules/.bin`, both the bundled tools and the project's;
  - `/opt/st/stm32cube{ide,clt}_*`.

  It applies user overrides and picks the newest version. `Toolchain` becomes a facade over it, and only
  today's six tools stay required.
- **Child processes:** `src/process/ManagedProcess` runs each one in its own process group with a
  parent-death signal and ready-line detection. `DevServer` is rebuilt on top of it.
- **Toolbars:** named toolbar groups, with a View → Toolbars menu.
- **Panels:** panels can be closed, with a View → Panels menu and per-project-type defaults.
- **Project kinds:** `src/core/ProjectKinds` holds per-kind traits. The scattered `if (kind == …)` checks
  become `switch` statements, so the compiler finds every place a new kind touches. (Done: the traits
  table. The `switch` conversion moves to S1, where the new kind lands.)

### Web / SvelteKit (first)

**W1 SvelteKit projects** (done)
- **Project setup:**
  - Flavour detection.
  - A "SvelteKit App" template, created with `npx sv create`.
  - An Install Dependencies command.
- **Running and checking:**
  - Run starts Vite on 127.0.0.1 with a known port.
  - F9 runs `svelte-kit sync` and `svelte-check`, with results in the Compiler panel.
  - Project → Scripts runs any package.json script.
- **Editor:** Svelte syntax highlighting (`data/language-specs/svelte.lang`) and a file icon.

**W2 Svelte IntelliSense** (done; ESLint and Tailwind moved to the backlog)
- **Svelte servers:** svelte-language-server for `.svelte` files, plus typescript-svelte-plugin inside the
  TypeScript server for `.ts` files that import components.
- **More servers per file:** LspManager can route one file to several servers. This brings in the ESLint and
  Tailwind language servers when a project uses them.
- **Protocol work:**
  - Real answers to `workspace/configuration`.
  - The `$/onDidChangeTsOrJsFile` notification.
  - `svelte-kit sync` runs before the server starts.

**W3 SvelteKit Explorer** (done)
- **Routes panel:** shows groups, params, `+page`/`+layout`/`+server`, load functions, actions, HTTP verbs,
  remote functions and the layout chain.
- **Navigation:** load → props data flow, and a "New Route…" command.
- **Env manager:** a `$env` static/dynamic × private/public matrix that warns when a private variable leaks
  into client code.

**W4 Full-stack debugging** (done)
- **Debugger plumbing:** the DAP client and the `DebugBackend` interface.
- **js-debug:** fetched as a pinned, checksummed release.
- **F5:** one key debugs `+page.server.ts` and `.svelte` code together, in the server and the browser.

**W5 Tests, traces, dependency health** (done)
- **Tests panel:** for Vitest and Playwright, using their JSON reporters.
- **Traces:** a loopback OTLP collector with a Traces waterfall.
- **Dependencies panel:** fed by `npm outdated` and `npm audit`.

### STM32

**S1 STM32 projects and build**
- **The Stm32 kind,** with three new core modules:
  - `src/core/Xml` (libxml2);
  - `src/core/Stm32`, for `.ioc` parsing and the `.cproject` → CMake import;
  - `src/core/CMakePresets`.
- **Builds:** CMake presets with the ARM toolchain, and clangd `--query-driver` for `arm-none-eabi-*`.
- **Status bar:** FLASH and RAM usage % after each build.
- **CubeMX:** "Open in CubeMX", plus a reconfigure when CubeMX regenerates code.

**S2 STM32 toolbar: probes, flash, serial**

The toolbar group appears on its own for STM32 projects and can be toggled from View. It has:
- a probe picker showing name and serial, with hot-plug;
- Flash, Erase and Reset;
- Debug on target and Attach;
- a Serial Monitor on the ST-Link's virtual COM port;
- error hints, such as pointing at the udev rules when access is denied.

**S3 On-target debugging**
- **Launch:** remote gdb through `ST-LINK_gdbserver`, OpenOCD or st-util, halting at `main`.
- **Breakpoint kinds:** IF, live and value-change breakpoints already go through DebugController's gdb
  code. On the target: live breakpoints are dprintf through gdb's Python; an arm-none-eabi-gdb built
  without Python gets the stop-read-continue fallback, which is slow over SWD, so say so. Watchpoints
  use the Cortex-M's few comparators (2–4); report gdb's "too many hardware watchpoints" plainly.
- **Views:** Registers and Memory.
- **Faults:** a Cortex-M HardFault decoder that jumps to the faulting line.

**S4 Peripherals and live data**
- **Peripherals:** SVD peripheral registers with decoded, editable fields.
- **Live data:** watch and plot variables without halting the CPU.
- **Console:** RTT and SWO.

### GTK designer (native)

**G1 Model, preview, properties** (done)
- **Design page:** `.ui` files open as a Design | XML page in the editor area.
- **Editing:** selection, an outline, a property editor and undo.
- **Saving:** a save changes only what was edited.
- **As built:** `core/UiDocument` keeps every node's original text and writes untouched nodes back as
  they were (checked against every GtkBuilder file under /usr/share). Undo stores whole-text snapshots.
  A child's layout properties (grid cells, packing) aren't editable yet; they come with G2's slots.

**G2 Palette and drag-and-drop** (done)
- **Palette:** a widget palette.
- **Layout editing:**
  - Drop widgets into any container slot, move them, and place them in grid cells.
  - Cut, copy and paste.
  - "Wrap in Box/Grid/ScrolledWindow".
- **Signals:** a Signals tab with handler names.
- **As built:** `data/designer/catalog.json` lists the palette and each container's slots; `core/UiEdit`
  places, moves, wraps and removes objects to match. A drop goes to the innermost container under the
  pointer with room for it. Moving is by drag in the design or Move Up/Down (not in the outline yet).

**G3 Code-behind and GTK app projects** (done)
- **Generators:** gtkmm, C and Blueprint, plus "Go to handler".
- **Templates:** new "GTK 4 Application (gtkmm)" and "GTK 4 Application (C)".
- **End result:** a designed window builds and runs with F11.
- **As built:** `core/UiCodegen`. Code is generated for windows with an id, not templates. Both
  languages embed the .ui without its `<signal>`s and connect the handlers in code. gtkmm handlers take
  no arguments and return void or bool; the widgets are members. C uses `g_signal_connect`, and weak
  empty stand-ins make a project link before its handlers are written (instead of a
  GtkBuilderCScope). Generated files are rewritten on save once they exist; the user's files are
  created if missing. The tests compile and link the generated code against the real gtkmm and GTK.

**G4 Kev-C++ in Kev-C++** (started 2026-11-01)
- **Goal:** Kev-C++ is developed in Kev-C++, its windows included. Open the Kev-C++ folder, open a
  window's `.ui` file, change it on the Design page, save, press F9, and F10 runs the changed Kev-C++.
  Nothing about it needs a text editor or another tool.
- **How:** each window or panel gets a `.ui` file beside its class (`src/ui/SplashWindow.ui` next to
  `SplashWindow.cpp`), handled exactly as in a user's gtkmm project: the designer writes
  `SplashWindow.ui.h/.ui.cpp` on every save (a base class `SplashWindowUi`, the design embedded as a
  string, so nothing is installed or looked up at run time) and the existing class derives from it.
  The class keeps what a design can't hold: data, lists filled at run time, timers, event
  controllers. The generated files are committed, and a test fails if one is out of date.
- **Steps:**
  1. **The loop works** (done 2026-11-01): the generated `create()` passes constructor arguments
     on; the designer copes with a project that keeps its own CMakeLists.txt (no pkg-config package
     added; it says when a generated file isn't in CMakeLists.txt yet); the splash became
     `SplashWindow.ui`. Checked by doing it in a copy of the repository: Kev-C++ opened in Kev-C++,
     Code → Write gtkmm Code, the file added to CMakeLists.txt in the editor, F9; then the subtitle
     changed on the Design page, saved, F11, and the new Kev-C++'s splash showed it. Gap found: the
     preview shows a window as a box, so `window.kevc-splash` CSS (the splash's gradient) doesn't
     apply there (step 5).
  2. **Dialogs:** New Project, Project Options, the npm Packages dialog, the small question and
     message dialogs, Toolchain info, About and Check for Updates.
  3. **Panels:** a base class for a top-level widget that isn't a window (a Box or Notebook with an
     id), for the panels inside the main window: Search, Debug (Locals, Call Stack, Breakpoints,
     Console), Watches, Compiler, Tests, Traces, Environment, Dependencies, Routes, Symbols and the
     Welcome tab. MainWindow holds some of these by value today; they become pointers.
  4. **The main window:** its frame (menu row, toolbar rows, the three docks, status bar) and its
     menus as `<menu>`s in the design. What changes while it runs (which toolbar groups and panels
     show, the docks' contents) stays in code, which fills named places in the design.
  5. **Designer gaps** these turn up, fixed as found. Expected: Kev-C++'s own look in the preview of
     its own designs (Slate rather than GTK's default), widgets GtkBuilder can't make (Kev-C++'s
     C++-only classes) shown as named placeholders, editing `<menu>`s, CSS classes in the properties.
- **Picking it up again** (paused 2026-11-01 after step 1; the user resumes in a few days with step 2):
  - **Moving one window, the recipe the splash followed:** write `src/ui/<Class>.ui` with the window's
    layout (top-level id = the class name, so the designer leaves the existing `<Class>.h/.cpp` alone;
    ids that code needs end in `_`); open it in Kev-C++, Code → Write gtkmm Code; add
    `src/ui/<Class>.ui.cpp` to CMakeLists.txt; make the class derive from `<Class>Ui` with a
    constructor `(BaseObjectType*, const Glib::RefPtr<Gtk::Builder>&, …its own arguments)` and a
    static `create(…)`; delete the layout code; keep data, timers, controllers and lists in code.
    Callers change from `new X(…)` or a member to `X::create(…)`.
  - **Order for step 2**, simplest first: Toolchain info and About (in MainWindow), the message and
    question boxes in `Dialogs.cpp` (promptText, askChoice, showError, showInfo: one shared design),
    New Project (`Dialogs.cpp`), Check for Updates, Project Options, then the npm Packages dialog
    (its rows are built at run time; only the frame is designed).
  - **Testing it:** in a copy of the repository (rsync without `build/` and `.git`) pre-built with
    `cmake -S . -B build/debug -G Ninja -DCMAKE_BUILD_TYPE=Debug` and `cmake --build build/debug -- -k 0`,
    opened in Kev-C++ on Xephyr. Then the designer writes the code, F9 builds only what changed, and
    F10/F11 runs the new Kev-C++ on the same display. The copy has no git history, so its version
    reads "unknown".
  - **Known gaps** for step 5: the preview shows windows as boxes, so `window.<class>` CSS doesn't
    apply there; the generator makes a base class only for windows (panels need step 3's change).
- **Done when** every Kev-C++ window and panel has a `.ui` file, a change made on the Design page
  shows in the next F10 run, and the tests pass.
- **Risks:** converting the main window touches docking, focus and key handling, which are checked
  only by driving the app, so it goes last and gets the longest GUI test.

### C/C++ pro

**C1 Tests and code quality**
- **Tests:** CMake presets for every C++ project, and a CTest/GoogleTest/Catch2 test explorer that can run
  or debug a single test.
- **Formatting and linting:** clang-format (including on save) and clang-tidy with fix-its.

**C2 Runtime analysis**
- **Sanitizers:** ASan/UBSan and TSan build configs with clickable reports.
- **Valgrind:** memcheck.
- **Coverage:** a gutter display, with per-test coverage.

**C3 Build insight**
- **Compile times:** a profile from `.ninja_log` and `-ftime-report`. (gcc has no `-ftime-trace`.)
- **Binary size:** a treemap with a diff against the previous build, shared with STM32.
- **Disassembly:** a disassembly view.

### Backlog (not scheduled)

- FreeRTOS-aware threads. This is the first candidate, since the user has FreeRTOS projects.
- probe-rs over DAP, and multi-core STM32.
- A Svelte component playground.
- Remote build and debug over SSH.
- Designer support for templates and custom widgets.
- Designer follow-ups from G1–G3:
  - Drag and drop in the outline (moving is by drag in the design or Move Up/Down today).
  - gtkmm handlers that receive the signal's arguments.
  - A light/dark switch for the preview: it shows GTK's dark theme, where the app may run light.
  - Showing other stack and notebook pages in the preview, not only the visible one.
  - Checking the Blueprint export with blueprint-compiler when it's installed.
- Existing gaps (Replace, Find Results, saved breakpoints and recent projects are done): the bundled
  TypeScript dev server can't load npm packages.
- Kev-C++'s own windows in the designer: they're built in code; moving them, dialogs first, to `.ui`
  files loaded with Gtk::Builder would let the designer edit them (see docs/IMPORTANT_TODO.md).
- ESLint and Tailwind language servers (from W2). LspManager has to route one file to several
  servers and merge their diagnostics.
- From M0: Toolchain Info listing the tools ToolLocator finds (e.g. both STM32CubeIDE toolchains),
  with `Toolchain` as a facade over ToolLocator.
- From W4: one `DebugBackend` interface over gdb and js-debug. Today MainWindow sends each action to
  whichever debugger is running.

## Risks worth knowing up front

**Web**
- js-debug isn't on npm, so the build has to download a pinned release.
- Child debug sessions are the hardest part of the DAP client.
- Browser debugging needs Chrome or Chromium.
- Svelte tooling has to stay on TypeScript 5.9/6, because TypeScript 7 has no plugin API yet.
- Node installed through nvm keeps causing PATH problems.

**STM32**
- No ST-Link is connected to the development machine, so hardware checks are manual.
- The `.cproject` format varies between CubeIDE versions.
- ST's tools can only be detected, not bundled.
- Reading live values while the target runs depends on the backend.

**Designer**
- The gtkmm 4.10 headers are older than the GTK 4.14 runtime. (Handled: generated code only names a
  gtkmm class whose header is installed, else `Gtk::Widget`.)
- Some widget combinations may misbehave in an in-process preview.
- Blueprint syntax is still changing.

## Verification (every milestone)

- **Build and tests:** `cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build`.
  New `src/core` logic gets unit tests.
- **GUI checks in Xephyr :5:**
  - Use a temporary `XDG_CONFIG_HOME` and `XDG_CACHE_HOME`.
  - Open projects from the command line.
  - Point the browser, Chrome and probe tools at recording scripts.
  - Stop test instances by their command line.
- **Hardware checks:** STM32 checks that need a real ST-Link are listed separately as manual steps.

## Key sources

- **SvelteKit**
  - Observability (OpenTelemetry): https://svelte.dev/docs/kit/observability
  - Debugging docs: https://svelte.dev/docs/kit/debugging
  - `--inspect` breakpoints not hit: https://github.com/sveltejs/kit/issues/7781
  - Svelte 5 devtools: https://github.com/sveltejs/svelte-devtools/issues/193
  - Language server: https://github.com/sveltejs/language-tools/tree/master/packages/language-server
  - Stale diagnostics without `$/onDidChangeTsOrJsFile`: https://github.com/sveltejs/language-tools/issues/2008
  - js-debug DAP server: https://github.com/microsoft/vscode-js-debug/issues/1388
- **STM32**
  - STM32CubeIDE 2.0: https://community.st.com/stm32cubeide-mcus-28/stm32cubeide-2-0-0-released-159542
  - CLion live watches: https://blog.jetbrains.com/clion/2025/05/introducing-live-watches/
  - Cortex-Debug: https://github.com/marus/cortex-debug
  - Memfault on HardFaults: https://interrupt.memfault.com/blog/cortex-m-fault-debug
  - OpenOCD udev rules (ST-Link PIDs): https://raw.githubusercontent.com/openocd-org/openocd/master/contrib/60-openocd.rules
  - probe-rs probe setup: https://probe.rs/docs/getting-started/probe-setup/
  - FreeRTOS + OpenOCD helper: https://github.com/FreeRTOS/FreeRTOS-Kernel/issues/33
- **GTK**
  - Cambalache 1.0: https://www.phoronix.com/news/Cambalache-1.0-GTK-UIs
  - GtkBuilderCScope: https://docs.gtk.org/gtk4/class.BuilderCScope.html
  - gtkmm derived widgets: https://developer.gnome.org/gtkmm-tutorial/stable/sec-builder-using-derived-widgets.html.en
  - gtkmm signal auto-connect: https://bugzilla.gnome.org/show_bug.cgi?id=573482
- **C/C++**
  - 2026 ISO C++ developer survey: https://isocpp.org/files/papers/CppDevSurvey-2026-summary.pdf
  - ClangBuildAnalyzer: https://github.com/aras-p/ClangBuildAnalyzer
