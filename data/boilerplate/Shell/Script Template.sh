---
description: A bash script that stops on errors and unset variables, with usage() and main()
file: script.sh
whole-file: yes
order: 10
section: starters
---
#!/usr/bin/env bash
# One line saying what this script does.
set -euo pipefail

usage() {
    cat <<EOF
Usage: ${0##*/} [-h] FILE...

What the script does, in a sentence or two.

  -h, --help    show this help
EOF
}

main() {
    if [[ $# -eq 0 ]]; then
        usage >&2
        exit 1
    fi
    if [[ $1 == -h || $1 == --help ]]; then
        usage
        exit 0
    fi
    {{cursor}}
}

main "$@"
