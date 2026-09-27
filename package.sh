#!/usr/bin/env bash
# Build and package exTracker binaries into a distributable tarball.
# Usage:  ./package.sh [build-dir]
# Default build dir: build-make (must already be configured).

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${1:-${REPO_DIR}/build-make}"
DATE="$(date +%Y%m%d)"
COMMIT="$(git -C "${REPO_DIR}" rev-parse --short HEAD 2>/dev/null || echo "local")"
PKG_NAME="extracker-${DATE}-${COMMIT}"
OUT_DIR="${REPO_DIR}/${PKG_NAME}"
TARBALL="${REPO_DIR}/${PKG_NAME}.tar.gz"

echo "=== exTracker packager ==="
echo "Build dir : ${BUILD_DIR}"
echo "Package   : ${TARBALL}"
echo ""

# 1. Build latest binaries
echo "Building CLI…"
cmake --build "${BUILD_DIR}" --target extracker
echo "Building GUI…"
cmake --build "${BUILD_DIR}" --target extracker_gui

echo ""
echo "Collecting files…"
rm -rf "${OUT_DIR}"
mkdir -p "${OUT_DIR}/bin"

cp "${BUILD_DIR}/extracker"     "${OUT_DIR}/bin/"
cp "${BUILD_DIR}/extracker_gui" "${OUT_DIR}/bin/"
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
else
    DEST="${HOME}/.local/bin"
    mkdir -p "${DEST}"
    echo "Installing to ${DEST}…"
    install -m 755 "${SCRIPT_DIR}/bin/extracker"     "${DEST}/extracker"
    install -m 755 "${SCRIPT_DIR}/bin/extracker_gui" "${DEST}/extracker_gui"
fi

# Write a launcher for the GUI that auto-connects Keystation if present
LAUNCHER="${DEST}/extracker-gui"
cat > "${LAUNCHER}" << 'LAUNCHER_EOF'
#!/usr/bin/env bash
# Launch exTracker GUI and auto-connect a Keystation 88 MK3 if detected.
extracker_gui &
GUI_PID=$!

for i in $(seq 1 50); do
    ET_CLIENT=$(aconnect -o 2>/dev/null | awk '/exTracker MIDI Input/{match($0,/client ([0-9]+)/,a);print a[1];exit}')
    if [[ -n "${ET_CLIENT}" ]]; then
        KB=$(aconnect -i 2>/dev/null | awk '/Keystation 88/{match($0,/client ([0-9]+)/,a);print a[1];exit}')
        if [[ -n "${KB}" ]]; then
            aconnect "${KB}:0" "${ET_CLIENT}:0" && echo "Connected Keystation 88 MK3 → exTracker"
        else
            echo "No Keystation found — connect manually if needed."
        fi
        break
    fi
    sleep 0.1
done

wait $GUI_PID
LAUNCHER_EOF
chmod 755 "${LAUNCHER}"

echo ""
echo "Installed:"
echo "  ${DEST}/extracker       — CLI (REPL)"
echo "  ${DEST}/extracker_gui   — GUI"
echo "  ${DEST}/extracker-gui   — GUI launcher (auto-connects MIDI)"
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
  extracker-gui       Launch the GUI (auto-connects Keystation 88 if detected)
  extracker_gui       Launch the GUI directly

FILES IN THIS TARBALL
  bin/extracker       CLI binary (stripped)
  bin/extracker_gui   GUI binary (stripped)
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
