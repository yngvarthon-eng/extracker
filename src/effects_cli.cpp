#include "extracker/effects_cli.hpp"

#include <iostream>
#include <string>
#include <cstdint>

namespace extracker {

static const char* distTypeName(DistortionType t) {
    switch (t) {
        case DistortionType::Soft: return "soft";
        case DistortionType::Hard: return "hard";
        case DistortionType::Fuzz: return "fuzz";
        default:                   return "off";
    }
}

static DistortionType parseDistType(const std::string& s) {
    if (s == "soft" || s == "1") return DistortionType::Soft;
    if (s == "hard" || s == "2") return DistortionType::Hard;
    if (s == "fuzz" || s == "3") return DistortionType::Fuzz;
    return DistortionType::Off;
}

static float norm(int v) { return static_cast<float>(v) / 255.0f; }

void handleEffectsCommand(AudioEngine& audio, PluginHost& plugins, Sequencer& sequencer,
                           std::istringstream& input) {
    std::string sub;
    input >> sub;

    auto applyBoth = [&](std::uint8_t instr, const InstrumentEffectParams& p) {
        audio.setInstrumentEffects(instr, p);
        plugins.setInstrumentEffects(instr, p);
    };

    if (sub == "delay") {
        // effects delay <instr> <time_ms 0-1000> <feedback 0-255> <wet 0-255>
        int instr = -1; int timeMs = 375, fb = 100, wet = 128;
        if (!(input >> instr >> timeMs >> fb >> wet)
            || instr < 0 || instr >= static_cast<int>(PluginHost::kMaxInstrumentSlots)) {
            std::cout << "Usage: effects delay <instr 0-15> <time_ms 0-1000> <feedback 0-255> <wet 0-255>\n";
            return;
        }
        auto p = plugins.getInstrumentEffectParams(static_cast<std::uint8_t>(instr));
        p.delay.timeMs   = static_cast<float>(std::clamp(timeMs, 0, 2000));
        p.delay.feedback = norm(std::clamp(fb, 0, 255)) * 0.98f;
        p.delay.wet      = norm(std::clamp(wet, 0, 255));
        applyBoth(static_cast<std::uint8_t>(instr), p);
        std::cout << "Instr " << instr << " delay: time=" << p.delay.timeMs
                  << "ms feedback=" << p.delay.feedback << " wet=" << p.delay.wet << '\n';

    } else if (sub == "distortion" || sub == "dist") {
        // effects dist <instr> <off|soft|hard|fuzz> <drive 0-255>
        int instr = -1; std::string typeStr; int drive = 128;
        if (!(input >> instr >> typeStr >> drive)
            || instr < 0 || instr >= static_cast<int>(PluginHost::kMaxInstrumentSlots)) {
            std::cout << "Usage: effects dist <instr 0-15> <off|soft|hard|fuzz> <drive 0-255>\n";
            return;
        }
        auto p = plugins.getInstrumentEffectParams(static_cast<std::uint8_t>(instr));
        p.distortion.type  = parseDistType(typeStr);
        p.distortion.drive = norm(std::clamp(drive, 0, 255));
        p.distortion.mix   = 1.0f;
        applyBoth(static_cast<std::uint8_t>(instr), p);
        std::cout << "Instr " << instr << " distortion: " << distTypeName(p.distortion.type)
                  << " drive=" << p.distortion.drive << '\n';

    } else if (sub == "chorus") {
        // effects chorus <instr> <rate 0-255> <depth 0-255> <wet 0-255>
        int instr = -1; int rate = 64, depth = 128, wet = 128;
        if (!(input >> instr >> rate >> depth >> wet)
            || instr < 0 || instr >= static_cast<int>(PluginHost::kMaxInstrumentSlots)) {
            std::cout << "Usage: effects chorus <instr 0-15> <rate 0-255> <depth 0-255> <wet 0-255>\n";
            return;
        }
        auto p = plugins.getInstrumentEffectParams(static_cast<std::uint8_t>(instr));
        p.chorus.rate  = 0.05f + norm(std::clamp(rate, 0, 255)) * 4.95f;
        p.chorus.depth = norm(std::clamp(depth, 0, 255));
        p.chorus.wet   = norm(std::clamp(wet, 0, 255));
        applyBoth(static_cast<std::uint8_t>(instr), p);
        std::cout << "Instr " << instr << " chorus: rate=" << p.chorus.rate
                  << "Hz depth=" << p.chorus.depth << " wet=" << p.chorus.wet << '\n';

    } else if (sub == "clear") {
        int instr = -1;
        if (!(input >> instr) || instr < 0 || instr >= static_cast<int>(PluginHost::kMaxInstrumentSlots)) {
            std::cout << "Usage: effects clear <instr 0-15>\n";
            return;
        }
        audio.clearInstrumentEffects(static_cast<std::uint8_t>(instr));
        plugins.clearInstrumentEffects(static_cast<std::uint8_t>(instr));
        std::cout << "Instr " << instr << " effects cleared\n";

    } else if (sub == "depth") {
        // effects depth <instr 0-15> <depth 0-255>  (0=front 255=rear)
        int instr = -1, depth = -1;
        if (!(input >> instr >> depth)
            || instr < 0 || instr >= static_cast<int>(PluginHost::kMaxInstrumentSlots)) {
            std::cout << "Usage: effects depth <instr 0-15> <depth 0-255>\n";
            return;
        }
        const float d = norm(std::clamp(depth, 0, 255));
        const auto u8 = static_cast<std::uint8_t>(instr);
        audio.setInstrumentDepth(u8, d);
        plugins.setInstrumentDepth(u8, d);
        std::cout << "Instr " << instr << " depth=" << d << " (0=front 1=rear)\n";

    } else if (sub == "reset") {
        // effects reset [<instr 0-15>|all]  — clears effects + filter + pitch
        std::string arg;
        input >> arg;
        auto resetOne = [&](std::uint8_t i) {
            audio.clearInstrumentEffects(i);
            plugins.clearInstrumentEffects(i);
            audio.clearInstrumentFilter(i);
            plugins.clearInstrumentFilter(i);
            audio.setInstrumentPitch(i, 0.0f);
            plugins.setInstrumentPitch(i, 0.0f);
            audio.setInstrumentReverbSend(i, 0.0f);
            plugins.setInstrumentReverbSend(i, 0.0f);
            audio.setInstrumentDepth(i, 0.0f);
            plugins.setInstrumentDepth(i, 0.0f);
        };
        if (arg.empty() || arg == "all") {
            for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(PluginHost::kMaxInstrumentSlots); ++i)
                resetOne(i);
            std::cout << "All instruments: effects/filter/pitch reset\n";
        } else {
            int instr = -1;
            try { instr = std::stoi(arg); } catch (...) {}
            if (instr < 0 || instr >= static_cast<int>(PluginHost::kMaxInstrumentSlots)) {
                std::cout << "Usage: effects reset [<instr 0-15>|all]\n";
                return;
            }
            resetOne(static_cast<std::uint8_t>(instr));
            std::cout << "Instr " << instr << " effects/filter/pitch reset\n";
        }

    } else if (sub == "get") {
        int instr = -1;
        if (!(input >> instr) || instr < 0 || instr >= static_cast<int>(PluginHost::kMaxInstrumentSlots)) {
            std::cout << "Usage: effects get <instr 0-15>\n";
            return;
        }
        const auto p = plugins.getInstrumentEffectParams(static_cast<std::uint8_t>(instr));
        std::cout << "Instr " << instr << ":\n";
        std::cout << "  delay:      time=" << p.delay.timeMs << "ms feedback="
                  << p.delay.feedback << " wet=" << p.delay.wet << '\n';
        std::cout << "  distortion: " << distTypeName(p.distortion.type)
                  << " drive=" << p.distortion.drive << '\n';
        std::cout << "  chorus:     rate=" << p.chorus.rate << "Hz depth="
                  << p.chorus.depth << " wet=" << p.chorus.wet << '\n';

    } else if (sub == "list") {
        std::cout << "Instr  Delay(ms/fb/wet)        Dist(type/drive)  Chorus(rate/depth/wet)\n";
        for (std::uint8_t i = 0; i < PluginHost::kMaxInstrumentSlots; ++i) {
            const auto p = plugins.getInstrumentEffectParams(i);
            if (!p.isActive()) continue;
            char line[120];
            std::snprintf(line, sizeof(line),
                "%-6u %-6.0f/%-5.2f/%-5.2f  %-4s/%-6.2f  %.2fHz/%.2f/%.2f\n",
                static_cast<unsigned>(i),
                static_cast<double>(p.delay.timeMs),
                static_cast<double>(p.delay.feedback),
                static_cast<double>(p.delay.wet),
                distTypeName(p.distortion.type),
                static_cast<double>(p.distortion.drive),
                static_cast<double>(p.chorus.rate),
                static_cast<double>(p.chorus.depth),
                static_cast<double>(p.chorus.wet));
            std::cout << line;
        }

    } else {
        std::cout << "Usage: effects <delay|dist|chorus|depth|clear|reset|get|list>\n"
                  << "  effects delay    <instr> <time_ms 0-1000> <feedback 0-255> <wet 0-255>\n"
                  << "  effects dist     <instr> <off|soft|hard|fuzz> <drive 0-255>\n"
                  << "  effects chorus   <instr> <rate 0-255> <depth 0-255> <wet 0-255>\n"
                  << "  effects depth    <instr> <depth 0-255>  (0=front 255=rear)\n"
                  << "  effects clear    <instr>           (effects only)\n"
                  << "  effects reset    [<instr>|all]     (effects + filter + pitch + depth)\n"
                  << "  effects get      <instr>\n"
                  << "  effects list\n"
                  << "Effect codes: 1B=delay_time 1C=delay_fb 1D=delay_wet "
                     "1E=dist_type 1F=dist_drive 20=chorus_rate 21=chorus_depth 22=chorus_wet\n";
    }
}

}  // namespace extracker
