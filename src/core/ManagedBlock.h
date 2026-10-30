#pragma once

#include <optional>
#include <stdexcept>
#include <string>

namespace kevc {

// The region of a CMakeLists.txt or Makefile that Kev-C++ regenerates. Everything outside
// the markers belongs to the user and is preserved byte-for-byte.
namespace managed_block {

inline constexpr const char* kBeginMarker = "# >>> Kev-C++ managed";
inline constexpr const char* kBeginNotice = " - edits inside this block are overwritten";
inline constexpr const char* kEndMarker = "# <<< Kev-C++ managed";

struct MalformedBlock : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Returns the body between the markers, or nullopt if the file has no block.
// Throws MalformedBlock if a begin marker has no matching end marker (or vice versa).
std::optional<std::string> extract(const std::string& text);

// Returns `text` with the block body replaced by `body`. If the file has no block, the block is
// appended at the end. `body` should end with a newline. Throws MalformedBlock on broken markers.
std::string splice(const std::string& text, const std::string& body);

// The complete block (markers plus body), for writing new files.
std::string wrap(const std::string& body);

} // namespace managed_block
} // namespace kevc
