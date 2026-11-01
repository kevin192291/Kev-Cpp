# Important to-dos

Done items say where to find them. Last updated 2026-11-01.

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
- ✅ **The .kevc holds the project's settings**: toolbars and panels shown or hidden and the Debug /
  Release choice (`"ui"`). Web folders have no .kevc by design, so theirs are kept with the project's
  remembered state (`~/.local/state/kev-cpp/projects.json`).

## Partly done

- ◐ **Kev-C++ developed with Kev-C++**: it opens its own folder with "Build As It Is"
  (`Kev-Cpp.kevc`; Kev-C++ never edits its CMakeLists.txt), builds with F9, runs a second Kev-C++
  with F10 and debugs it with F5.
  **Still to do:** editing Kev-C++'s windows in the visual designer. They're built in C++ code, not
  `.ui` files, so each window (starting with the dialogs) has to move to a `.ui` file loaded with
  Gtk::Builder before the designer can open it. That's milestone-sized; plan it first.

## Not done

- The bundled TypeScript dev server (projects without Vite) can't load npm packages.
- Floating panel windows don't remember where they were (GTK 4 can't position windows). Not wanted.
