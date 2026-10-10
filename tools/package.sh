#!/usr/bin/env bash
# Package already-built Release binaries into a distributable tarball.
# Run it through ./x package, which builds build/release first.
# Usage:  tools/package.sh <build-dir>

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:?usage: tools/package.sh <build-dir>}"
DATE="$(date +%Y%m%d)"
COMMIT="$(git -C "${REPO_DIR}" rev-parse --short HEAD 2>/dev/null || echo "local")"
PKG_NAME="extracker-${DATE}-${COMMIT}"
OUT_DIR="${REPO_DIR}/${PKG_NAME}"
TARBALL="${REPO_DIR}/${PKG_NAME}.tar.gz"

echo "=== exTracker packager ==="
echo "Build dir : ${BUILD_DIR}"
echo "Package   : ${TARBALL}"
echo ""

BUILD_TYPE="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "${BUILD_DIR}/CMakeCache.txt")"
if [[ "${BUILD_TYPE}" != "Release" ]]; then
  echo "Warning: ${BUILD_DIR} is a '${BUILD_TYPE:-default}' build, not Release." >&2
fi

echo ""
echo "Collecting files…"
rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/bin"

cp "${BUILD_DIR}/extracker"     "${OUT_DIR}/bin/"
cp "${BUILD_DIR}/extracker_gui" "${OUT_DIR}/bin/"
cp "${REPO_DIR}/tools/run-gui.sh" "${OUT_DIR}/bin/extracker-gui"
strip --strip-unneeded "${OUT_DIR}/bin/extracker" 2>/dev/null || true
strip --strip-unneeded "${OUT_DIR}/bin/extracker_gui" 2>/dev/null || true

# 2. Write the install script
cat > "${OUT_DIR}/install.sh" << 'INSTALL'
#!/usr/bin/env bash
# Install exTracker binaries to ~/.local/bin  (or /usr/local/bin with --system).
set -euo pipefail

SYSTEM=0
for arg in "$@"; do [[ "$arg" == "--system" ]] && SYSTEM=1; done

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [[ $SYSTEM -eq 1 ]]; then
    DEST=/usr/local/bin
    echo "Installing to ${DEST} (needs sudo)…"
    sudo install -m 755 "${SCRIPT_DIR}/bin/extracker"     "${DEST}/extracker"
    sudo install -m 755 "${SCRIPT_DIR}/bin/extracker_gui" "${DEST}/extracker_gui"
    sudo install -m 755 "${SCRIPT_DIR}/bin/extracker-gui" "${DEST}/extracker-gui"
else
    DEST="${HOME}/.local/bin"
    mkdir -p "${DEST}"
    echo "Installing to ${DEST}…"
    install -m 755 "${SCRIPT_DIR}/bin/extracker"     "${DEST}/extracker"
    install -m 755 "${SCRIPT_DIR}/bin/extracker_gui" "${DEST}/extracker_gui"
    install -m 755 "${SCRIPT_DIR}/bin/extracker-gui" "${DEST}/extracker-gui"
fi

echo ""
echo "Installed:"
echo "  ${DEST}/extracker       — CLI (REPL)"
echo "  ${DEST}/extracker_gui   — GUI"
echo "  ${DEST}/extracker-gui   — GUI launcher (uses ~/.config/extracker/env)"
echo ""
echo "Required system packages (install if missing):"
echo "  Ubuntu/Debian:"
echo "    sudo apt install libasound2 libjack-jackd2-0 libpipewire-0.3-0 \\"
echo "         libfluidsynth3 libsndfile1 libcurl4 libfreetype6"
echo "  Arch:"
echo "    sudo pacman -S alsa-lib jack2 pipewire fluidsynth libsndfile curl freetype2"
INSTALL
chmod 755 "${OUT_DIR}/install.sh"

# 3. Write a short README
cat > "${OUT_DIR}/README.txt" << README
exTracker  ${DATE} (${COMMIT})
================================
Linux-first pattern-based music tracker — CLI + JUCE GUI.

QUICK INSTALL
  ./install.sh              # installs to ~/.local/bin
  ./install.sh --system     # installs to /usr/local/bin (needs sudo)

REQUIRED LIBRARIES (must be present on the target system)
  Ubuntu/Debian:
    sudo apt install libasound2 libjack-jackd2-0 libpipewire-0.3-0 \
         libfluidsynth3 libsndfile1 libcurl4 libfreetype6

  Arch Linux:
    sudo pacman -S alsa-lib jack2 pipewire fluidsynth libsndfile curl freetype2

USAGE
  extracker           Start the CLI REPL (type 'help' for commands)
  extracker-gui       Launch the GUI with ~/.config/extracker/env applied
  extracker_gui       Launch the GUI directly

FILES IN THIS TARBALL
  bin/extracker       CLI binary (stripped)
  bin/extracker_gui   GUI binary (stripped)
  bin/extracker-gui   GUI launcher script
  install.sh          Installer script
  README.txt          This file
README

# 4. Pack
echo "Creating ${TARBALL}…"
tar -czf "${TARBALL}" -C "${REPO_DIR}" "${PKG_NAME}"
rm -rf "${OUT_DIR}"

SIZE=$(du -sh "${TARBALL}" | cut -f1)
echo ""
echo "Done — ${TARBALL} (${SIZE})"
echo ""
echo "Copy to the other machine:"
echo "  scp ${TARBALL} user@host:~/"
echo "  ssh user@host 'tar xzf ${PKG_NAME}.tar.gz && cd ${PKG_NAME} && ./install.sh'"
