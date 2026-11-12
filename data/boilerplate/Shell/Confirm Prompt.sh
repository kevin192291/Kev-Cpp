---
description: confirm(): asks a yes/no question; anything but y or yes (or no answer at all) means no
order: 51
section: output
---
# if confirm "Delete 3 files?"; then ... fi
confirm() {
    local answer=""
    read -r -p "$1 [y/N] " answer || true
    [[ ${answer,,} == y || ${answer,,} == yes ]]
}
