#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace kevc::mi {

using json = nlohmann::json;

// One line of GDB/MI output.
struct Record {
    enum class Type {
        Result,  // ^done / ^running / ^error / ^exit / ^connected
        Exec,    // *stopped / *running
        Status,  // +download...
        Notify,  // =breakpoint-modified, =thread-group-exited, ...
        Console, // ~"text"  (gdb's own console output)
        Target,  // @"text"  (inferior output, when not on a tty)
        Log,     // &"text"  (gdb's log / echoed commands)
        Prompt,  // (gdb)
        Unknown,
    };

    Type type = Type::Unknown;
    std::optional<int> token;
    std::string resultClass; // "done", "stopped", "breakpoint-modified", ...
    json results = json::object();
    std::string text; // stream records: the decoded C string; Unknown: the raw line
};

// Parses one line (without the trailing newline). Never throws: malformed lines become Unknown.
Record parseLine(std::string_view line);

// Quotes an argument for an MI command: foo -> "foo" with C escapes.
std::string quote(std::string_view text);

// Pointer types whose pointee is worth showing next to the address (not char*, not function pointers).
bool isDataPointer(std::string_view type);
// "0x0" / "0x0000000000000000"
bool isNullPointer(std::string_view value);

// The C/C++ expression under the cursor at `column` (character index) of `line`, extended
// leftwards through member access (., ->, ::), subscripts and a unary * or &. Hovering `x` in
// `p->pos.x` gives `p->pos.x`; hovering `p` gives `p`. Empty if the column is not on an identifier.
std::string expressionAt(std::string_view line, int column);

} // namespace kevc::mi
