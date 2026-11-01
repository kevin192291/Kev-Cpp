#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kevc {

// The kinds of breakpoint on a line. Standard stops there. Conditional ("IF") stops only when its
// condition is true. Live never stops: each time the line runs, the `show` expressions are read and
// shown next to the line. The fourth kind, "break when a value changes", isn't on a line: it's a
// watch on an expression (MainWindow keeps those as plain strings).
enum class BreakKind { Standard, Conditional, Live };

struct Breakpoint {
    BreakKind kind = BreakKind::Standard;
    std::string condition;         // Conditional: e.g. i == 41
    std::vector<std::string> show; // Live: e.g. {"i", "total"}

    bool operator==(const Breakpoint&) const = default;
};

using FileBreakpoints = std::map<int, Breakpoint>; // 1-based line -> breakpoint
using BreakpointMap = std::map<std::filesystem::path, FileBreakpoints>;

const char* kindName(BreakKind kind); // "standard", "if", "live" (as saved)

// "i, total, f(a, b)" -> {"i", "total", "f(a, b)"}: commas inside brackets or quotes don't split.
std::vector<std::string> splitExpressions(std::string_view text);
std::string joinExpressions(const std::vector<std::string>& expressions);
// What a live breakpoint shows: "i = 41, total = 1523.5".
std::string liveSummary(const std::vector<std::string>& names, const std::vector<std::string>& values);

// JavaScript live breakpoints are js-debug logpoints. Their message carries a marker so their output
// can be told from the program's: the breakpoint's id and each expression's value.
std::string liveLogMessage(int id, const std::vector<std::string>& show);
struct LiveLog {
    int id = 0;
    std::vector<std::string> values;
};
std::optional<LiveLog> parseLiveLog(std::string_view output);

// C/C++ live breakpoints are gdb dprintf breakpoints: gdb prints the values and carries on without
// reporting a stop, so stepping isn't interrupted either. Each value goes through a small Python
// function, since a dprintf whose expression fails (a name not in scope) halts the program without
// saying so; the function turns the error into "<message>" instead. gdbLivePython() is the console
// command that defines $_kevc_value("expr") and $_kevc_locals(); a gdb without Python refuses it,
// and live breakpoints then stop for a moment instead (DebugController).
std::string gdbLivePython();
// The format and arguments of -dprintf-insert (each still to be MI-quoted). No expressions: every
// local, as "name = value" items.
std::vector<std::string> gdbLiveArguments(int id, const std::vector<std::string>& show);
// "name = value" items (the locals form) -> names and values.
std::pair<std::vector<std::string>, std::vector<std::string>> splitNamedValues(const std::vector<std::string>& items);

// "Break when the value changes" in JavaScript, which has no watchpoints: Kev-C++ turns the property
// into a getter/setter whose setter stops (a `debugger` statement) when the value changes, so it
// stops at the assignment. Only properties can be watched: `player.score` -> ("player", "score"),
// `items[3]` -> ("items", "3"); a plain variable gives nullopt.
std::optional<std::pair<std::string, std::string>> splitPropertyPath(std::string_view expression);
// JavaScript expressions, evaluated while paused, that install and remove watch `id`'s trap. Both
// return "ok" or why not.
std::string jsWatchInstall(int id, const std::string& object, const std::string& property);
std::string jsWatchRemove(int id);
// Evaluated after a stop: "id⁣old⁣new" if the stop was a watched value changing (and
// clears it), else "".
std::string jsWatchLastChange();
struct WatchChange {
    int id = 0;
    std::string before, after;
};
std::optional<WatchChange> parseWatchChange(std::string_view text);

// Saved form (ProjectState): standard breakpoints as plain line numbers, as before; the others as
// {"line", "kind", "condition" / "show"}.
nlohmann::json toJson(const FileBreakpoints& file);
FileBreakpoints fileBreakpointsFromJson(const nlohmann::json& lines);

} // namespace kevc
