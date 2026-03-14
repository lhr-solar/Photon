#!/usr/bin/env bash
# Package Photon into a .deb (builds Release by default)
# Example: ./makeDeb.sh -v 1.0.0 -b Release

set -euo pipefail
umask 022

usage() {
    cat <<'EOF'
Usage: ./makeDeb.sh [-v version] [-b build_type]
  -v  Package version (defaults to git tag + commit or 1.0.0)
  -b  CMake build type (Debug/Release/RelWithDebInfo), default: Release
EOF
}

VERSION=""
BUILD_TYPE="Release"
while getopts ":v:b:h" opt; do
    case "$opt" in
        v) VERSION="$OPTARG" ;;
        b) BUILD_TYPE="$OPTARG" ;;
        h) usage; exit 0 ;;
        *) usage >&2; exit 1 ;;
    esac
done

require_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "[-] Missing required command: $1" >&2
        exit 1
    fi
}

require_cmd cmake
require_cmd ninja
require_cmd dpkg-deb
require_cmd dpkg

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ARTIFACT_DIR="$ROOT_DIR/artifacts"
BUILD_DIR="$ARTIFACT_DIR/deb-build"
STAGING_DIR="$ARTIFACT_DIR/deb-stage"
APP_PREFIX="/opt/photon"
APP_STAGING="$STAGING_DIR$APP_PREFIX"
DEBIAN_DIR="$STAGING_DIR/DEBIAN"
ARCH="$(dpkg --print-architecture)"

if [[ -z "$VERSION" ]]; then
    if git -C "$ROOT_DIR" describe --tags --abbrev=0 >/dev/null 2>&1; then
        BASE_VERSION="$(git -C "$ROOT_DIR" describe --tags --abbrev=0 | sed 's/^v//')"
    else
        BASE_VERSION="1.0.0"
    fi
    if git -C "$ROOT_DIR" rev-parse --short HEAD >/dev/null 2>&1; then
        VERSION="${BASE_VERSION}+git$(git -C "$ROOT_DIR" rev-parse --short HEAD)"
    else
        VERSION="$BASE_VERSION"
    fi
fi

echo "[+] Building Photon ${VERSION} (${BUILD_TYPE}) for ${ARCH}"
rm -rf "$BUILD_DIR" "$STAGING_DIR"
mkdir -p "$BUILD_DIR" "$APP_STAGING" "$DEBIAN_DIR" "$STAGING_DIR/usr/bin"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_C_COMPILER=/usr/bin/gcc \
    -DCMAKE_CXX_COMPILER=/usr/bin/g++
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel

BIN_PATH="$BUILD_DIR/photon/$BUILD_TYPE/Photon"
if [[ ! -x "$BIN_PATH" ]]; then
    ALT_BIN="$(find "$BUILD_DIR" -maxdepth 4 -type f -name Photon -print -quit)"
    if [[ -n "$ALT_BIN" ]]; then
        BIN_PATH="$ALT_BIN"
    else
        echo "[-] Built Photon binary not found in $BUILD_DIR" >&2
        exit 1
    fi
fi

install -m 0755 "$BIN_PATH" "$APP_STAGING/Photon"
install -d -m 0755 "$APP_STAGING/assets"
install -d -m 0755 "$APP_STAGING/artifacts/assets/kernels/spirv"
cp -r "$ROOT_DIR/assets/." "$APP_STAGING/assets/"

if [[ -f "$ROOT_DIR/config.ini" ]]; then
    install -m 0644 "$ROOT_DIR/config.ini" "$APP_STAGING/config.ini"
fi
if [[ -f "$ROOT_DIR/README" ]]; then
    install -m 0644 "$ROOT_DIR/README" "$APP_STAGING/README"
fi
printf '%s\n' "$VERSION" > "$APP_STAGING/.version"

cat > "$STAGING_DIR/usr/bin/photon" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

INSTALL_ROOT="/opt/photon"
RUNTIME_ROOT="${XDG_DATA_HOME:-$HOME/.local/share}/photon"
INSTALLED_VERSION="$(cat "${INSTALL_ROOT}/.version" 2>/dev/null || echo unknown)"

mkdir -p "$RUNTIME_ROOT"
if [[ ! -f "$RUNTIME_ROOT/.version" || "$(cat "$RUNTIME_ROOT/.version")" != "$INSTALLED_VERSION" ]]; then
    cp -r "${INSTALL_ROOT}/." "$RUNTIME_ROOT/"
    echo "$INSTALLED_VERSION" > "$RUNTIME_ROOT/.version"
    chmod +x "$RUNTIME_ROOT/Photon"
fi

cd "$RUNTIME_ROOT"
exec "$RUNTIME_ROOT/Photon" "$@"
EOF
chmod 0755 "$STAGING_DIR/usr/bin/photon"

cat > "$DEBIAN_DIR/control" <<EOF
Package: photon
Version: $VERSION
Section: misc
Priority: optional
Architecture: $ARCH
Maintainer: Photon Developers <maintainer@example.com>
Depends: libc6, libstdc++6, libvulkan1, libxcb1
Description: Photon heterogeneous compute engine
 Installs Photon under /opt/photon and runs from a user-local copy.
EOF

if command -v strip >/dev/null 2>&1; then
    strip --strip-unneeded "$APP_STAGING/Photon" || true
fi

OUTPUT_DEB="$ARTIFACT_DIR/photon_${VERSION}_${ARCH}.deb"
dpkg-deb --build "$STAGING_DIR" "$OUTPUT_DEB"

echo "[+] .deb created at $OUTPUT_DEB"
echo "[i] Install with: sudo dpkg -i \"$OUTPUT_DEB\""
