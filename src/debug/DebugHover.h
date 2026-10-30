#pragma once

#include <gtksourceview/gtksource.h>

#include <functional>

namespace kevc {

// Shows variable values while the debugger is paused. `handler` returns false when it has nothing
// to offer (not debugging, no expression under the mouse); otherwise it calls `show` exactly once,
// with a widget or nullptr. Returns a new reference.
using DebugHoverHandler =
    std::function<bool(const GtkTextIter& at, std::function<void(GtkWidget* widget)> show)>;
GtkSourceHoverProvider* newDebugHoverProvider(DebugHoverHandler handler);

} // namespace kevc
