#!/usr/bin/env bash
# Packages a Release build for GitHub: kev-cpp-<version>-linux-x86_64.tar.gz holding the program, its
# bundled tools (language servers, js-debug, dev server, trace collector) and install.sh
# (packaging/install-release.sh). Also writes VERSION and notes.md next to it. The CI workflow
# (.github/workflows/release.yml) runs this; it works locally too.
#   packaging/make-release.sh BUILD_DIR OUT_DIR
set -euo pipefail

SRC=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
BUILD=$(realpath "${1:?usage: make-release.sh BUILD_DIR OUT_DIR}")
OUT=$(realpath -m "${2:?usage: make-release.sh BUILD_DIR OUT_DIR}")

VERSION=$(sed -n 's/^#define KEVC_BUILD_VERSION "\(.*\)"$/\1/p' "$BUILD/generated/kevc_version.h")
[[ -n $VERSION && $VERSION != unknown ]] || { echo "error: no version in $BUILD/generated/kevc_version.h" >&2; exit 1; }
[[ $VERSION != *+changes ]] || { echo "error: $VERSION has uncommitted changes; release a commit" >&2; exit 1; }
NAME=kev-cpp-$VERSION

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
PKG=$STAGE/$NAME
cmake --install "$BUILD" --prefix "$PKG" >/dev/null
# install.sh writes the launcher and the icon cache for wherever the package is installed: these
# were made for the staging folder (and an icon cache here would replace the user's whole cache).
rm -rf "$PKG/share/applications"
rm -f "$PKG/share/icons/hicolor/icon-theme.cache"
cp "$SRC/packaging/install-release.sh" "$PKG/install.sh"
chmod 755 "$PKG/install.sh"
cp "$SRC/data/org.kevcpp.KevCpp.desktop.in" "$PKG/"
printf '%s\n' "$VERSION" >"$PKG/VERSION"

mkdir -p "$OUT"
tar -czf "$OUT/$NAME-linux-x86_64.tar.gz" -C "$STAGE" "$NAME"
printf '%s\n' "$VERSION" >"$OUT/VERSION"
{
    git -C "$SRC" log -1 --format='%s%n%n%b' 2>/dev/null || true
    echo "Built from commit $(git -C "$SRC" rev-parse HEAD 2>/dev/null || echo unknown)."
    echo
    echo "Install: download the .tar.gz, unpack it and run ./install.sh, or use Help → Check for Updates in Kev-C++."
} >"$OUT/notes.md"
if [[ -n ${GITHUB_OUTPUT:-} ]]; then echo "version=$VERSION" >>"$GITHUB_OUTPUT"; fi
echo "$OUT/$NAME-linux-x86_64.tar.gz"
