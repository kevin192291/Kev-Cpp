## Found Bugs:

- ✅ std::cout isn't found in IntelliSense. Fixed: completion ranks what was typed first and asks
  clangd again when its list is incomplete. `std::co` lists `cout` (after the shorter `cos`
  overloads); `std::cou` puts it first. Checked 2026-11-01.
