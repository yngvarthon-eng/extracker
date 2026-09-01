#!/bin/bash
# Launch extracker GUI and connect any attached Keystation 88 MK3
cd "$(dirname "$0")/build-make-gui"
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
