#!/usr/bin/env bash
# Installs a Kev-C++ release package (this file is install.sh inside it): the program, its bundled
# tools and a menu entry, into ~/.local unless told otherwise. Help → Check for Updates runs it.
# Missing tools and libraries are installed with apt, after asking (sudo).
set -euo pipefail

PKG=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PREFIX=$HOME/.local
ASSUME_YES=0
DESKTOP_ID=org.kevcpp.KevCpp.desktop

usage() {
    cat <<EOF
Usage: ./install.sh [--prefix DIR] [-y]

Installs Kev-C++ $(cat "$PKG/VERSION" 2>/dev/null) from this package.
  --prefix DIR   install under DIR (default: ~/.local)
  -y, --yes      don't ask before installing missing apt packages
EOF
}

step() { printf '\n==> %s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }
have() { command -v "$1" >/dev/null 2>&1; }
writable() {
    local d=$1
    while [[ ! -e $d ]]; do d=$(dirname "$d"); done
    [[ -w $d ]]
}

while (($#)); do
    case $1 in
        --prefix) (($# >= 2)) || die "--prefix needs a directory"; PREFIX=$2; shift ;;
        --prefix=*) PREFIX=${1#*=} ;;
        -y | --yes) ASSUME_YES=1 ;;
        -h | --help) usage; exit 0 ;;
        *) usage >&2; die "unknown option: $1" ;;
    esac
    shift
done

((EUID != 0)) || die "run this as your normal user; it uses sudo only for the steps that need it"
VERSION=$(cat "$PKG/VERSION")
PREFIX=$(realpath -m "$PREFIX")
SUDO=()
writable "$PREFIX" || SUDO=(sudo)

# --- What Kev-C++ needs to run ---
missing=()
need() { have "$1" || missing+=("$2"); }
need gcc build-essential
need g++ build-essential
need make build-essential
need cmake cmake
need gdb gdb
need git git
need clangd clangd
need curl curl
# The libraries the program links against: GTK 4, gtkmm, GtkSourceView, VTE.
if ldd "$PKG/bin/kev-cpp" 2>/dev/null | grep -q "not found"; then
    missing+=(libgtkmm-4.0-0 libgtksourceview-5-0 libvte-2.91-gtk4-0)
fi
if ((${#missing[@]})); then
    mapfile -t missing < <(printf '%s\n' "${missing[@]}" | sort -u)
    if have apt-get; then
        step "Installing what Kev-C++ needs: ${missing[*]}"
        yes=()
        if ((ASSUME_YES)); then yes=(-y); fi
        sudo apt-get install "${yes[@]}" "${missing[@]}" || warn "some packages didn't install; Kev-C++ may not start"
    else
        warn "install these packages (Debian/Ubuntu names): ${missing[*]}"
    fi
fi
node_major() { have node && node -p 'process.versions.node.split(".")[0]' 2>/dev/null || echo 0; }
if (($(node_major) < 20)); then
    warn "Node.js 20 or newer is needed for TypeScript/Svelte IntelliSense, the dev server and web debugging"
    warn "(https://github.com/nvm-sh/nvm installs one without root)"
fi

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

# --- Files ---
step "Installing Kev-C++ $VERSION into $PREFIX"
# Write next to the target and rename over it: replacing a program that's running is fine that way.
put() { # source destination mode
    "${SUDO[@]}" install -D -m "$3" "$1" "$2.new"
    "${SUDO[@]}" mv -f "$2.new" "$2"
}
put "$PKG/bin/kev-cpp" "$PREFIX/bin/kev-cpp" 755
put "$PKG/bin/kev" "$PREFIX/bin/kev" 755
while IFS= read -r -d '' icon; do
    put "$icon" "$PREFIX/${icon#"$PKG/"}" 644
done < <(find "$PKG/share/icons" -type f -print0)
# The bundled tools and the Boilerplate menu's snippets, each as a whole: the new copy first, then
# swap it in (snippets removed from Kev-C++ go too; the user's own are in ~/.config/kev-cpp).
"${SUDO[@]}" mkdir -p "$PREFIX/share/kev-cpp"
for part in tools boilerplate; do
    [[ -d $PKG/share/kev-cpp/$part ]] || continue
    dest=$PREFIX/share/kev-cpp/$part
    "${SUDO[@]}" rm -rf "$dest.new" "$dest.old"
    "${SUDO[@]}" cp -a "$PKG/share/kev-cpp/$part" "$dest.new"
    if [[ -e $dest ]]; then "${SUDO[@]}" mv "$dest" "$dest.old"; fi
    "${SUDO[@]}" mv "$dest.new" "$dest"
    "${SUDO[@]}" rm -rf "$dest.old"
done
printf '%s\n' "$VERSION" | "${SUDO[@]}" tee "$PREFIX/share/kev-cpp/VERSION" >/dev/null

# --- Menu entry ---
# The launcher runs the program by full path (~/.local/bin is often not on the desktop's PATH). If
# node comes from a version manager such as nvm, the menu wouldn't find it, so put its folder on the
# launcher's PATH (the same as the source install.sh does).
desktop=$PREFIX/share/applications/$DESKTOP_ID
tmp=$(mktemp)
sed "s|@BINDIR@|$PREFIX/bin|g" "$PKG/org.kevcpp.KevCpp.desktop.in" >"$tmp"
if have node; then
    node_dir=$(dirname "$(command -v node)")
    safe='^[A-Za-z0-9/._+-]+$'
    case ":/usr/local/bin:/usr/bin:/bin:/snap/bin:$HOME/.local/bin:" in
        *":$node_dir:"*) ;;
        *)
            if [[ $node_dir =~ $safe && $PREFIX =~ $safe ]]; then
                # Desktop files unescape \\ to \ before splitting Exec like a shell command, so this writes:
                # sh -c "PATH=<node_dir>:\\$PATH exec <bin>/kev-cpp \\"\\$@\\"" kev-cpp %F
                exec_line=$(printf 'Exec=sh -c "PATH=%s:\\\\$PATH exec %s/bin/kev-cpp \\\\"\\\\$@\\\\"" kev-cpp %%F' "$node_dir" "$PREFIX")
                patched=$(mktemp)
                while IFS= read -r line; do
                    [[ $line == Exec=* ]] && line=$exec_line
                    printf '%s\n' "$line"
                done <"$tmp" >"$patched"
                mv "$patched" "$tmp"
                echo "Menu launcher uses Node from $node_dir"
            fi
            ;;
    esac
fi
put "$tmp" "$desktop" 644
rm -f "$tmp"
"${SUDO[@]}" gtk-update-icon-cache -f -t "$PREFIX/share/icons/hicolor" >/dev/null 2>&1 || true
"${SUDO[@]}" update-desktop-database "$PREFIX/share/applications" >/dev/null 2>&1 || true
gio set "$PREFIX/bin/kev-cpp" metadata::custom-icon \
    "file://$PREFIX/share/icons/hicolor/256x256/apps/org.kevcpp.KevCpp.png" >/dev/null 2>&1 || true

cat <<EOF

Kev-C++ $VERSION is installed.
  Program:   $PREFIX/bin/kev-cpp
  Terminal:  kev [folder]   (opens its .kevc project, or the folder; package.json → TypeScript)
  Menu:      Programming → Kev-C++
EOF
link_kev
