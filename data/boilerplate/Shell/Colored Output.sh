---
description: info, ok, warn and error helpers that print in colour on a terminal and plain text elsewhere
order: 50
section: output
---
# Colour only when both outputs are terminals (not files or pipes), and never when NO_COLOR is set.
if [[ -t 1 && -t 2 && -z "${NO_COLOR:-}" ]]; then
    red=$'\e[31m' green=$'\e[32m' yellow=$'\e[33m' bold=$'\e[1m' reset=$'\e[0m'
else
    red='' green='' yellow='' bold='' reset=''
fi

info() { printf '%s\n' "${bold}$*${reset}"; }
ok() { printf '%s\n' "${green}ok:${reset} $*"; }
warn() { printf '%s\n' "${yellow}warning:${reset} $*" >&2; }
error() { printf '%s\n' "${red}error:${reset} $*" >&2; }
