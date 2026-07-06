#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/.artifacts"
APPDIR="${BUILD_DIR}/Photon.AppDir"
APPIMAGE="${BUILD_DIR}/bin/Photon.AppImage"
ICON_ICO="${ROOT_DIR}/assets/fonts/Photon.ico"
ICON_PNG="${APPDIR}/Photon.png"

if [[ "$(uname -s)" != "Linux" ]]; then
  echo "AppImage builds must run on Linux." >&2
  exit 1
fi

if ! command -v appimagetool >/dev/null 2>&1; then
  echo "appimagetool was not found in PATH." >&2
  echo "Install appimagetool, then rerun this script." >&2
  exit 1
fi

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
cmake --build "${BUILD_DIR}" -j"$(nproc)"

rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin" "${APPDIR}/usr/share/applications" "${APPDIR}/usr/share/icons/hicolor/256x256/apps"

cp "${BUILD_DIR}/bin/Photon" "${APPDIR}/usr/bin/Photon"

cat > "${APPDIR}/AppRun" <<'EOF'
#!/usr/bin/env sh
HERE="$(dirname "$(readlink -f "$0")")"
exec "$HERE/usr/bin/Photon" "$@"
EOF
chmod +x "${APPDIR}/AppRun"

cat > "${APPDIR}/Photon.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Photon
Exec=Photon
Icon=Photon
Categories=Utility;
Terminal=false
EOF
cp "${APPDIR}/Photon.desktop" "${APPDIR}/usr/share/applications/Photon.desktop"

if command -v magick >/dev/null 2>&1; then
  magick "${ICON_ICO}[0]" -resize 256x256 "${ICON_PNG}"
elif command -v convert >/dev/null 2>&1; then
  convert "${ICON_ICO}[0]" -resize 256x256 "${ICON_PNG}"
elif command -v icotool >/dev/null 2>&1; then
  icotool -x --width=256 --height=256 --output="${APPDIR}" "${ICON_ICO}"
  extracted_icon="$(find "${APPDIR}" -maxdepth 1 -type f -name '*.png' | head -n 1)"
  if [[ -z "${extracted_icon}" ]]; then
    echo "icotool did not extract a PNG icon from ${ICON_ICO}." >&2
    exit 1
  fi
  mv "${extracted_icon}" "${ICON_PNG}"
else
  echo "No ICO conversion tool found." >&2
  echo "Install ImageMagick or icoutils so ${ICON_ICO} can be converted to Photon.png." >&2
  exit 1
fi

cp "${ICON_PNG}" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/Photon.png"

appimagetool "${APPDIR}" "${APPIMAGE}"
chmod +x "${APPIMAGE}"

echo "${APPIMAGE}"
