#!/usr/bin/env bash
# Builds Kev-C++ (Release) and installs it with its launcher, which the applications menu lists under
# Programming (Categories=Development). Re-run it after pulling changes to update the installed copy.
set -euo pipefail

SRC_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
BUILD_DIR=$SRC_DIR/build/release    # inside build/, which is already git-ignored
DESKTOP_ID=org.kevcpp.KevCpp.desktop
PREFIX=$HOME/.local
ASSUME_YES=0
UNINSTALL=0

usage() {
    cat <<EOF
Usage: ./install.sh [options]

Builds Kev-C++ in Release mode and installs it, with a menu entry under Programming.
Missing build dependencies are installed with apt (you'll be asked for your sudo password).

  --prefix DIR   install under DIR (default: ~/.local, no root needed)
  --system       install for all users under /usr/local (uses sudo)
  -y, --yes      don't ask before installing missing apt packages
  --uninstall    remove what the last run installed
  -h, --help     show this help
EOF
}

step() { printf '\n==> %s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }

# True if the user can create files at $1 (or at the nearest directory above it that exists).
writable() {
    local d=$1
    while [[ ! -e $d ]]; do d=$(dirname "$d"); done
    [[ -w $d ]]
}

while (($#)); do
    case $1 in
        --prefix) (($# >= 2)) || die "--prefix needs a directory"; PREFIX=$2; shift ;;
        --prefix=*) PREFIX=${1#*=} ;;
        --system) PREFIX=/usr/local ;;
        -y | --yes) ASSUME_YES=1 ;;
        --uninstall) UNINSTALL=1 ;;
        -h | --help) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
    shift
done

# Under sudo, ~ and the per-user caches would belong to root.
((EUID != 0)) || die "run this as your normal user; it uses sudo only for the steps that need it"
PREFIX=$(realpath -m "$PREFIX")
SUDO=()
writable "$PREFIX" || SUDO=(sudo)

uninstall() {
    local manifest=$BUILD_DIR/install_manifest.txt
    [[ -f $manifest ]] || die "nothing to uninstall: $manifest not found"
    local exe prefix
    exe=$(grep -m1 '/bin/kev-cpp$' "$manifest") || die "$manifest doesn't list kev-cpp"
    prefix=${exe%/bin/kev-cpp}
    # The manifest drives rm, so refuse it if anything in it lies outside that prefix.
    awk -v p="$prefix/" 'NF && index($0, p) != 1 { bad = 1 } END { exit bad }' "$manifest" ||
        die "$manifest lists files outside $prefix; not removing anything"

    local sudo=()
    writable "$prefix" || sudo=(sudo)
    step "Removing Kev-C++ from $prefix"
    # CMake writes the launcher with file(WRITE), which the manifest doesn't record.
    { cat "$manifest"; echo; echo "$prefix/share/applications/$DESKTOP_ID"; } |
        grep . | xargs -d '\n' "${sudo[@]}" rm -f --
    if [[ -d $prefix/share/kev-cpp ]]; then
        "${sudo[@]}" find "$prefix/share/kev-cpp" -depth -type d -empty -delete
    fi
    "${sudo[@]}" gtk-update-icon-cache -f -t "$prefix/share/icons/hicolor" >/dev/null 2>&1 || true
    "${sudo[@]}" update-desktop-database "$prefix/share/applications" >/dev/null 2>&1 || true
    rm -f "$manifest"
    # The link that makes kev work in every terminal (see link_kev), if it's ours.
    local link=${KEVC_LINK_DIR:-/usr/local/bin}/kev
    if [[ -L $link && $(readlink "$link") == "$prefix/bin/kev" ]]; then
        local link_sudo=()
        writable "$(dirname "$link")" || link_sudo=(sudo)
        "${link_sudo[@]}" rm -f "$link"
    fi
    echo "Kev-C++ is uninstalled. Settings in ~/.config/kev-cpp were left in place."
}

node_major() {
    have node && node -p 'process.versions.node.split(".")[0]' 2>/dev/null || echo 0
}

# Major version of the distribution's nodejs package (Debian 13 / Devuan 6: 20; Ubuntu 24.04: 18).
apt_nodejs_major() {
    local v
    v=$(apt-cache policy nodejs 2>/dev/null | awk '/Candidate:/ { print $2 }')
    v=${v#*:} # epoch
    [[ $v =~ ^([0-9]+) ]] && echo "${BASH_REMATCH[1]}" || echo 0
}

check_node() {
    (($(node_major) >= 20)) || die "Node.js 20 or newer is required$(have node && echo ", but $(command -v node) is $(node --version)").
This distribution's nodejs package is too old; install a current Node with https://github.com/nvm-sh/nvm
(then open a new terminal and run ./install.sh again)"
    have npm || die "npm is required (it installs the TypeScript language server)"
}

install_deps() {
    local pkgs=()
    need() { have "$1" || pkgs+=("$2"); }
    need_pc() { pkg-config --exists "$1" 2>/dev/null || pkgs+=("$2"); }

    need gcc build-essential
    need g++ build-essential
    need make build-essential
    need cmake cmake
    need pkg-config pkg-config
    need glib-compile-resources libglib2.0-dev-bin
    need gdb gdb
    need git git
    need ninja ninja-build    # optional, but builds faster than make
    need clangd clangd        # C/C++ IntelliSense
    need_pc gtkmm-4.0 libgtkmm-4.0-dev
    need_pc gtksourceview-5 libgtksourceview-5-dev
    need_pc vte-2.91-gtk4 libvte-2.91-gtk4-dev
    if ! pkg-config --exists nlohmann_json 2>/dev/null &&
        [[ ! -f /usr/include/nlohmann/json.hpp && ! -f /usr/local/include/nlohmann/json.hpp ]]; then
        pkgs+=(nlohmann-json3-dev)
    fi
    local node_ok=1
    if (($(node_major) < 20)) || ! have npm; then node_ok=0; fi
    if ((${#pkgs[@]} == 0 && node_ok)); then return 0; fi

    have apt-get || die "missing dependencies (Debian/Ubuntu package names): ${pkgs[*]} and Node.js 20+ with npm"
    # A fresh system's package lists may be stale or empty.
    step "Updating the package lists"
    sudo apt-get update
    # Node.js 20+ for the TypeScript tools: the distribution's when it's new enough (Debian 13,
    # Devuan 6 and so LCOS ship 20); otherwise check_node explains how to get one.
    if ((!node_ok)) && (($(apt_nodejs_major) >= 20)); then
        if (($(node_major) < 20)); then pkgs+=(nodejs); fi
        pkgs+=(npm)
    fi
    ((${#pkgs[@]})) || return 0

    mapfile -t pkgs < <(printf '%s\n' "${pkgs[@]}" | sort -u)
    step "Installing missing packages: ${pkgs[*]}"
    local yes=()
    if ((ASSUME_YES)); then yes=(-y); fi
    sudo apt-get install "${yes[@]}" "${pkgs[@]}"
}

# The menu starts programs with the desktop session's PATH, which doesn't include Node version
# managers like nvm (they're set up in ~/.bashrc), so the IDE would get Ubuntu's Node 18 instead of
# the one used for this build. If node lives somewhere like that, put its directory on the
# launcher's PATH.
patch_launcher() {
    local desktop=$PREFIX/share/applications/$DESKTOP_ID node_dir
    node_dir=$(dirname "$(command -v node)")
    case ":/usr/local/bin:/usr/bin:/bin:/snap/bin:$HOME/.local/bin:" in
        *":$node_dir:"*) return 0 ;;
    esac
    # Keeps the Exec quoting below simple.
    local safe='^[A-Za-z0-9/._+-]+$'
    if [[ ! $node_dir =~ $safe || ! $PREFIX =~ $safe ]]; then
        warn "the menu launcher may not find $node_dir/node (unusual characters in a path)"
        return 0
    fi

    # Desktop files unescape \\ to \ before the Exec line is split like a shell command,
    # so this writes: sh -c "PATH=<node_dir>:\\$PATH exec <bin>/kev-cpp \\"\\$@\\"" kev-cpp %F
    local exec_line tmp
    exec_line=$(printf 'Exec=sh -c "PATH=%s:\\\\$PATH exec %s/bin/kev-cpp \\\\"\\\\$@\\\\"" kev-cpp %%F' \
        "$node_dir" "$PREFIX")
    tmp=$(mktemp)
    while IFS= read -r line; do
        [[ $line == Exec=* ]] && line=$exec_line
        printf '%s\n' "$line"
    done <"$desktop" >"$tmp"
    "${SUDO[@]}" install -m 644 "$tmp" "$desktop"
    rm -f "$tmp"
    echo "Menu launcher uses Node from $node_dir (re-run ./install.sh if you switch Node versions)"
}

# `kev` from any terminal. ~/.local/bin is only on PATH where the login reads ~/.profile, and
# Debian-based desktops (Devuan, MX, LCOS…) don't: there kev is linked into /usr/local/bin, which
# every PATH has (asking first: it needs sudo). KEVC_LINK_DIR changes where, for testing.
link_kev() {
    local bin=$PREFIX/bin dir=${KEVC_LINK_DIR:-/usr/local/bin} path=":$PATH:" answer
    # Kev-C++'s Terminal and Run tabs put its own folder on PATH and say so: other terminals don't get it.
    if [[ -n ${KEVC_TERMINAL_PATH_ADDED-} ]]; then path=${path/":$KEVC_TERMINAL_PATH_ADDED:"/:}; fi
    case $path in *":$bin:"*) return 0 ;; esac
    local link=$dir/kev
    if [[ -L $link && $(readlink -f "$link") == "$(readlink -f "$bin/kev")" ]]; then return 0; fi
    if [[ -e $link && ! -L $link ]]; then
        warn "$link already exists and isn't Kev-C++'s link; run $bin/kev by its full path"
        return 0
    fi
    if ((!ASSUME_YES)); then
        if [[ ! -t 0 ]]; then
            echo "Note: $bin isn't on your PATH, so terminals won't find kev; run $bin/kev by its full path."
            return 0
        fi
        read -r -p "Terminals won't find kev: $bin isn't on your PATH. Link it into $dir? [Y/n] " answer || answer=n
        if [[ -n $answer && $answer != [Yy]* ]]; then
            echo "Not linked: run $bin/kev by its full path."
            return 0
        fi
    fi
    local sudo=()
    writable "$dir" || sudo=(sudo)
    if "${sudo[@]}" mkdir -p "$dir" && "${sudo[@]}" ln -sfn "$bin/kev" "$link"; then
        echo "Linked $link → $bin/kev, so kev works in every terminal"
    else
        warn "couldn't link $link; run $bin/kev by its full path"
    fi
}

if ((UNINSTALL)); then
    uninstall
    exit 0
fi

install_deps
check_node

step "Configuring (Release) in $BUILD_DIR"
generator=()
if [[ ! -f $BUILD_DIR/CMakeCache.txt ]] && have ninja; then generator=(-G Ninja); fi
cmake -S "$SRC_DIR" -B "$BUILD_DIR" "${generator[@]}" \
    -DCMAKE_BUILD_TYPE=Release -DKEVC_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX="$PREFIX"

step "Building"
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

step "Installing to $PREFIX"
"${SUDO[@]}" "$(command -v cmake)" --install "$BUILD_DIR" --prefix "$PREFIX"
if ((${#SUDO[@]})); then
    # Otherwise a later install as the user couldn't overwrite it.
    sudo chown "$(id -u):$(id -g)" "$BUILD_DIR/install_manifest.txt"
fi
patch_launcher

cat <<EOF

Kev-C++ is installed.
  Program:   $PREFIX/bin/kev-cpp
  Launcher:  $PREFIX/share/applications/$DESKTOP_ID
  Menu:      Programming → Kev-C++
  Terminal:  kev [folder]   (opens its .kevc project, or the folder; package.json → TypeScript)
Uninstall with: ./install.sh --uninstall
EOF
link_kev
