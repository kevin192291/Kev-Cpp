## Found Bugs:

- ✅ std::cout isn't found in IntelliSense. Fixed: completion ranks what was typed first and asks
  clangd again when its list is incomplete. `std::co` lists `cout` (after the shorter `cos`
  overloads); `std::cou` puts it first. Checked 2026-11-01.
- ✅ The Project tab sometimes didn't show a new file (File → New Source File, or right-click a
  folder → New File) until its folder was closed and opened again. Fixed: GTK's folder listing
  stopped following a folder for good after any file in it was saved (saving writes a temporary file
  and renames it); the tree now lists and watches its folders itself. New files, renames and deletes
  show at once, after saves too. Checked 2026-11-05.
