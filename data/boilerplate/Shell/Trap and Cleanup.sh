---
description: A temporary folder that's deleted however the script ends: success, error, exit or Ctrl+C
order: 40
section: safety
---
# bash runs the EXIT trap on every way out: the end of the script, exit, a set -e failure, and Ctrl+C
# or kill too.
tmpdir=$(mktemp -d)
cleanup() {
    rm -rf "$tmpdir"
}
trap cleanup EXIT
