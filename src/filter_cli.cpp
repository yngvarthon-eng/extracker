#include "extracker/filter_cli.hpp"

#include <iostream>
#include <string>

namespace extracker {

static const char* biquadTypeName(BiquadType t) {
    switch (t) {
        case BiquadType::LowPass:  return "lp";
        case BiquadType::HighPass: return "hp";
        case BiquadType::BandPass: return "bp";
        case BiquadType::Notch:    return "notch";
        default:                   return "off";
    }
}

static BiquadType parseType(const std::string& s) {
    if (s == "lp"   || s == "lowpass"  || s == "low-pass"  || s == "1") return BiquadType::LowPass;
    if (s == "hp"   || s == "highpass" || s == "high-pass" || s == "2") return BiquadType::HighPass;
    if (s == "bp"   || s == "bandpass" || s == "band-pass" || s == "3") return BiquadType::BandPass;
    if (s == "notch"|| s == "n"        || s == "4")                     return BiquadType::Notch;
    return BiquadType::Off;
}

void handleFilterCommand(AudioEngine& audio, PluginHost& plugins, std::istringstream& input) {
    std::string sub;
    input >> sub;

    if (sub == "set") {
        int instr = -1;
        std::string typeStr;
        double cutoff = 0.5, resonance = 0.0;
        if (!(input >> instr >> typeStr >> cutoff >> resonance) ||
            instr < 0 || instr >= static_cast<int>(PluginHost::kMaxInstrumentSlots)) {
            std::cout << "Usage: filter set <instrument 0-15> <off|lp|hp|bp|notch> <cutoff 0..1> <resonance 0..1>\n";
            return;
        }
        const auto t  = parseType(typeStr);
        const auto c  = static_cast<float>(std::max(0.0, std::min(1.0, cutoff)));
        const auto r  = static_cast<float>(std::max(0.0, std::min(1.0, resonance)));
        const auto u8 = static_cast<std::uint8_t>(instr);
        audio.setInstrumentFilter(u8, t, c, r);
        plugins.setInstrumentFilter(u8, t, c, r);
        std::cout << "Instrument " << instr << " filter: " << biquadTypeName(t)
                  << " cutoff=" << c << " resonance=" << r << '\n';

    } else if (sub == "clear") {
        int instr = -1;
        if (!(input >> instr) || instr < 0 || instr >= static_cast<int>(PluginHost::kMaxInstrumentSlots)) {
            std::cout << "Usage: filter clear <instrument 0-15>\n";
            return;
        }
        const auto u8 = static_cast<std::uint8_t>(instr);
        audio.clearInstrumentFilter(u8);
        plugins.clearInstrumentFilter(u8);
        std::cout << "Instrument " << instr << " filter cleared\n";

    } else if (sub == "get") {
        int instr = -1;
        if (!(input >> instr) || instr < 0 || instr >= static_cast<int>(PluginHost::kMaxInstrumentSlots)) {
            std::cout << "Usage: filter get <instrument 0-15>\n";
            return;
        }
        const BiquadParams p = plugins.getInstrumentFilterParams(static_cast<std::uint8_t>(instr));
        std::cout << "Instrument " << instr << " filter: " << biquadTypeName(p.type)
                  << " cutoff=" << p.cutoffNorm << " resonance=" << p.resonanceNorm << '\n';

    } else if (sub == "list") {
        std::cout << "Instr  Type   Cutoff  Resonance\n";
        for (std::size_t i = 0; i < PluginHost::kMaxInstrumentSlots; ++i) {
            const BiquadParams p = plugins.getInstrumentFilterParams(static_cast<std::uint8_t>(i));
            if (!p.isActive()) continue;
            char line[80];
            std::snprintf(line, sizeof(line), "%-6zu %-6s %.3f   %.3f\n",
                          i, biquadTypeName(p.type), p.cutoffNorm, p.resonanceNorm);
            std::cout << line;
        }

    } else {
        std::cout << "Usage: filter <set|get|clear|list>\n"
                  << "  filter set <instr> <off|lp|hp|bp|notch> <cutoff 0..1> <resonance 0..1>\n"
                  << "  filter get <instr>\n"
                  << "  filter clear <instr>\n"
                  << "  filter list\n"
                  << "Effect codes: 18=type (0=off 1=lp 2=hp 3=bp 4=notch) "
                     "19=cutoff(0-255) 1A=resonance(0-255)\n";
    }
}

}  // namespace extracker
