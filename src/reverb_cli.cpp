#include "extracker/reverb_cli.hpp"

#include <algorithm>
#include <iostream>
#include <string>

namespace extracker {

static float norm8(int v) { return static_cast<float>(std::clamp(v, 0, 255)) / 255.0f; }

void handleReverbCommand(AudioEngine& audio, PluginHost& plugins, std::istringstream& input) {
    std::string sub;
    input >> sub;

    if (sub == "set") {
        // reverb set <room 0-255> <damp 0-255> <wet 0-255> [width 0-255]
        int room = -1, damp = -1, wet = -1, width = 255;
        if (!(input >> room >> damp >> wet)) {
            std::cout << "Usage: reverb set <room 0-255> <damp 0-255> <wet 0-255> [width 0-255]\n";
            return;
        }
        input >> width;
        ReverbParams p;
        p.roomSize = norm8(room);
        p.damping  = norm8(damp);
        p.wet      = norm8(wet);
        p.width    = norm8(width);
        audio.setReverbParams(p);
        std::cout << "Reverb: room=" << p.roomSize << " damp=" << p.damping
                  << " wet=" << p.wet << " width=" << p.width << '\n';

    } else if (sub == "send") {
        // reverb send <instr 0-15> <send 0-255>
        int instr = -1, send = -1;
        if (!(input >> instr >> send)
            || instr < 0 || instr >= static_cast<int>(PluginHost::kMaxInstrumentSlots)) {
            std::cout << "Usage: reverb send <instr 0-15> <send 0-255>\n";
            return;
        }
        const float s = norm8(send);
        const auto u8 = static_cast<std::uint8_t>(instr);
        audio.setInstrumentReverbSend(u8, s);
        plugins.setInstrumentReverbSend(u8, s);
        std::cout << "Instr " << instr << " reverb send=" << s << '\n';

    } else if (sub == "get") {
        const ReverbParams p = audio.getReverbParams();
        std::cout << "Reverb: room=" << p.roomSize << " damp=" << p.damping
                  << " wet=" << p.wet << " width=" << p.width << '\n';
        bool anySend = false;
        for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(PluginHost::kMaxInstrumentSlots); ++i) {
            const float s = audio.getInstrumentReverbSend(i);
            if (s > 0.0f) {
                std::cout << "  Instr " << static_cast<int>(i) << " send=" << s << '\n';
                anySend = true;
            }
        }
        if (!anySend) std::cout << "  (no instrument sends active)\n";

    } else if (sub == "clear") {
        audio.clearReverb();
        for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(PluginHost::kMaxInstrumentSlots); ++i)
            plugins.setInstrumentReverbSend(i, 0.0f);
        std::cout << "Reverb cleared\n";

    } else {
        std::cout << "Usage: reverb <set|send|get|clear>\n"
                  << "  reverb set  <room 0-255> <damp 0-255> <wet 0-255> [width 0-255]\n"
                  << "  reverb send <instr 0-15> <send 0-255>\n"
                  << "  reverb get\n"
                  << "  reverb clear\n";
    }
}

} // namespace extracker
