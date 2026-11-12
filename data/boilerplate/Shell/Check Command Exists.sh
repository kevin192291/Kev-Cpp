---
description: require(): stops the script with a clear message when a program it needs isn't installed
order: 41
section: safety
---
# command -v is built into the shell and works everywhere, unlike which.
require() {
    local cmd
    for cmd in "$@"; do
        if ! command -v "$cmd" >/dev/null 2>&1; then
            echo "This script needs $cmd; please install it first." >&2
            exit 1
        fi
    done
}

require git curl
