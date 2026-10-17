#!/bin/bash
# Launch extracker GUI and connect any attached Keystation 88 MK3
REPO_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$REPO_DIR/build-gui"

# Configure on first use (takes a few minutes: JUCE is fetched and compiled),
# then always rebuild so the GUI matches the checked-out sources. When nothing
# changed the incremental build finishes in a second or two.
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "No GUI build in $BUILD_DIR yet, configuring it..."
    cmake -S "$REPO_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug || exit 1
fi
cmake --build "$BUILD_DIR" -j"$(nproc)" --target extracker_gui || exit 1
cd "$BUILD_DIR" || exit 1

# Personal instrument library — not covered by the built-in default search
# paths, so point the SF2/SFZ/S3I scanners at it directly.
INSTRUMENTS_DIR="$HOME/Musikk/musicworks/instruments"
if [ -d "$INSTRUMENTS_DIR" ]; then
    export SF2_PATH="${SF2_PATH:-$INSTRUMENTS_DIR}"
    export SFZ_PATH="${SFZ_PATH:-$INSTRUMENTS_DIR}"
    export S3I_PATH="${S3I_PATH:-$INSTRUMENTS_DIR}"
    export XPM_PATH="${XPM_PATH:-$INSTRUMENTS_DIR}"
fi

./extracker_gui &
GUI_PID=$!

# Wait for exTracker MIDI Input port to appear (up to 5 seconds)
for i in $(seq 1 50); do
    EXTRACKER_CLIENT=$(aconnect -o 2>/dev/null | grep "exTracker MIDI Input" | sed -n 's/^client \([0-9]*\).*/\1/p' | head -n1)
    if [ -n "$EXTRACKER_CLIENT" ]; then
        KEYBOARD_CLIENT=$(aconnect -i 2>/dev/null | grep "Keystation 88" | sed -n 's/^client \([0-9]*\).*/\1/p' | head -n1)
        if [ -n "$KEYBOARD_CLIENT" ]; then
            aconnect "${KEYBOARD_CLIENT}:0" "${EXTRACKER_CLIENT}:0" && echo "Connected Keystation 88 MK3 → exTracker"
        else
            echo "Keystation not found — connect manually: aconnect 28:0 ${EXTRACKER_CLIENT}:0"
        fi
        break
    fi
    sleep 0.1
done

wait $GUI_PID
