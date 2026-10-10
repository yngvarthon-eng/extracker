#!/usr/bin/env bash
# Launch the exTracker GUI with this machine's personal settings.
# Usage: run-gui.sh [path/to/extracker_gui]   (default: extracker_gui on PATH)
#
# Machine-specific settings live outside the repo, in
# ${XDG_CONFIG_HOME:-~/.config}/extracker/env (a bash file), e.g.:
#
#   EXTRACKER_INSTRUMENTS_DIR="$HOME/Musikk/musikk/instruments"
#   EXTRACKER_MIDI_KEYBOARD="Keystation 88"   # part of the aconnect client name
#
# The instruments dir becomes the default SF2/SFZ/S3I/XPM search path, and the
# named MIDI keyboard is connected to exTracker's MIDI input once it appears.

GUI_BIN="${1:-extracker_gui}"

CONFIG_FILE="${XDG_CONFIG_HOME:-$HOME/.config}/extracker/env"
if [[ -f "$CONFIG_FILE" ]]; then
    # shellcheck source=/dev/null
    source "$CONFIG_FILE"
fi

if [[ -n "${EXTRACKER_INSTRUMENTS_DIR:-}" && -d "$EXTRACKER_INSTRUMENTS_DIR" ]]; then
    export SF2_PATH="${SF2_PATH:-$EXTRACKER_INSTRUMENTS_DIR}"
    export SFZ_PATH="${SFZ_PATH:-$EXTRACKER_INSTRUMENTS_DIR}"
    export S3I_PATH="${S3I_PATH:-$EXTRACKER_INSTRUMENTS_DIR}"
    export XPM_PATH="${XPM_PATH:-$EXTRACKER_INSTRUMENTS_DIR}"
fi

"$GUI_BIN" &
GUI_PID=$!

if [[ -n "${EXTRACKER_MIDI_KEYBOARD:-}" ]]; then
    # Wait for the exTracker MIDI Input port to appear (up to 5 seconds).
    for _ in $(seq 1 50); do
        EXTRACKER_CLIENT=$(aconnect -o 2>/dev/null | grep "exTracker MIDI Input" | sed -n 's/^client \([0-9]*\).*/\1/p' | head -n1)
        if [[ -n "$EXTRACKER_CLIENT" ]]; then
            KEYBOARD_CLIENT=$(aconnect -i 2>/dev/null | grep "$EXTRACKER_MIDI_KEYBOARD" | sed -n 's/^client \([0-9]*\).*/\1/p' | head -n1)
            if [[ -n "$KEYBOARD_CLIENT" ]]; then
                aconnect "${KEYBOARD_CLIENT}:0" "${EXTRACKER_CLIENT}:0" && echo "Connected $EXTRACKER_MIDI_KEYBOARD → exTracker"
            else
                echo "MIDI keyboard \"$EXTRACKER_MIDI_KEYBOARD\" not found; connect it with aconnect"
            fi
            break
        fi
        sleep 0.1
    done
fi

wait $GUI_PID
