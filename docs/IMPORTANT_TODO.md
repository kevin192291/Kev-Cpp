# Important to-dos

Done items say where to find them. Last updated 2026-11-04.

## Done

- ✅ **Right-click Go to Definition** in TypeScript, JavaScript and C/C++. The editor's right-click menu
  has Go to Definition, Go to Implementation (from an interface or virtual method to the code that
  implements it), Go to Type Definition, Go to Declaration (C/C++) and Find All References; several
  answers are listed at the cursor. TypeScript's Go to Definition goes on from a library's `.d.ts` to
  its JavaScript source ("Go to Source Definition", what VS Code took long to get).
- ✅ **C/C++: access specifiers** (`public:`, `private:`, `protected:`) have no indentation of their own:
  they snap to the class's level when the `:` is typed (core/CppStyle, editor/CppIndenter).
- ✅ **C/C++: `{` on its own line**: typing `{` after `class Foo`, a function or `if (…)` moves it to
  the next line (Allman style). Initialisers and lambdas keep theirs.
- ✅ **C++: the `;` after a class** is added when the `}` closing a class, struct, union or enum is
  typed.
- ✅ **Find in the current file and a global find**: Ctrl+F in a file (with Ctrl+R to replace), and the
  search field in the menu bar (Ctrl+P, files and text) and the Search tab (Ctrl+Shift+F, with
  Replace All).
- ✅ **No project open: Execute/Debug items and toolbar buttons greyed out** (their keys do nothing).
- ✅ **Four kinds of breakpoint** for C/C++ (gdb) and JavaScript (js-debug): standard, IF, live
  (shows values each time the line runs, without stopping) and break when a value changes.
  Right-click a line number, the Debug menu, or the Breakpoints tab (core/Breakpoints,
  DebugController, JsDebugger). STM32 gets them through the same gdb code (S3).
- ✅ **Closing or killing Kev-C++ while debugging JavaScript left Vite running** (js-debug's
  watchdog kept it waiting); stopped processes now take their whole tree with them, and SIGTERM
  closes the window properly.
- ✅ **The editor was cut off on the left** when the Debug panel was docked on the right; the docks
  now give way instead.
- ✅ **The .kevc holds the project's settings**: toolbars and panels shown or hidden and the Debug /
  Release choice (`"ui"`). Web folders have no .kevc by design, so theirs are kept with the project's
  remembered state (`~/.local/state/kev-cpp/projects.json`).
- ✅ **Ctrl+I indented C/C++ by 2 spaces** in projects without a .clang-format: it used the one in the
  home folder (LLVM style). Only a .clang-format in the project or its git repository counts now;
  otherwise Kev-C++'s layout (4 spaces) applies (core/Format's clangFormatConfig).

## Partly done

- ◐ **Kev-C++ developed with Kev-C++**: it opens its own folder with "Use Its Own CMakeLists.txt"
  (`Kev-Cpp.kevc`; Kev-C++ never edits its CMakeLists.txt), builds with F9, runs a second Kev-C++
  with F10 and debugs it with F5.
  **In progress: milestone G4** (docs/ROADMAP.md) moves Kev-C++'s windows to `.ui` files so the
  visual designer edits them. Done: the splash, every dialog (About, Toolchain, the message boxes,
  New Project, Project Options, npm Packages), the panels (Project, Search, Classes, Routes,
  Compiler, Compile Log, Run, Terminal, Debug, Watches, Environment, Tests, Traces, Dependencies, Dev
  Server, Welcome) and the main window with all its menus: change one on the Design page, save, F9,
  and F10 runs Kev-C++ with the change. Next: step 5, the designer gaps this showed (Kev-C++'s own
  look in the preview, C++-only widgets shown by name, a design wider than the canvas). Still built in
  code: the designer's page, the editor tab, the menu bar's search field and the floating panel
  windows.

## Not done

- The bundled TypeScript dev server (projects without Vite) can't load npm packages.
- Floating panel windows don't remember where they were (GTK 4 can't position windows). Not wanted.
