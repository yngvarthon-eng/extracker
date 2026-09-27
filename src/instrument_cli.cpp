#include "extracker/instrument_cli.hpp"

#include "extracker/cli_parse_utils.hpp"
#include "extracker/sample_editor_utils.hpp"

#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

namespace extracker {

namespace {

bool tryParseInstrumentToken(const std::string& token, int& outInstrument) {
  if (token.empty()) {
    return false;
  }
  char* end = nullptr;
  const long value = std::strtol(token.c_str(), &end, 10);
  if (end == nullptr || *end != '\0' || value < 0 ||
      value >= static_cast<long>(PluginHost::kMaxInstrumentSlots)) {
    return false;
  }
  outInstrument = static_cast<int>(value);
  return true;
}

bool tryParseSampleSlotToken(const std::string& token, int& outSlot) {
  if (token.empty()) {
    return false;
  }
  char* end = nullptr;
  const long value = std::strtol(token.c_str(), &end, 10);
  if (end == nullptr || *end != '\0' || value < 0 ||
      value >= static_cast<long>(PluginHost::kMaxSampleSlots)) {
    return false;
  }
  outSlot = static_cast<int>(value);
  return true;
}

void printInstrumentSummaryLine(PluginHost& plugins, int instrument) {
  const bool assigned = plugins.hasInstrumentAssignment(static_cast<std::uint8_t>(instrument));
  if (!assigned) {
    std::cout << "  [" << instrument << "] (unassigned)\n";
    return;
  }

  const std::string pluginId =
      plugins.pluginForInstrument(static_cast<std::uint8_t>(instrument));
  const int sampleSlot =
      plugins.sampleSlotForInstrument(static_cast<std::uint8_t>(instrument));

  std::cout << "  [" << instrument << "] " << pluginId;
  if (sampleSlot >= 0) {
    std::cout << " (sample slot " << sampleSlot << ")";
  }
  std::cout << "\n";
}

bool applyInstrumentParameter(PluginHost& plugins,
                              int instrument,
                              const std::string& parameterName,
                              double value) {
  const auto instrumentIndex = static_cast<std::uint8_t>(instrument);
  if (!plugins.hasInstrumentAssignment(instrumentIndex)) {
    std::cout << "Instrument " << instrument << " has no assigned plugin\n";
    return false;
  }

  if (!plugins.setInstrumentParameter(instrumentIndex, parameterName, value)) {
    std::cout << "Instrument " << instrument
              << " does not support parameter '" << parameterName << "'\n";
    return false;
  }

  std::cout << "Instrument " << instrument << " " << parameterName
            << " = " << value << "\n";
  return true;
}

void printInstrumentEditUsage() {
  std::cout << "Usage: instrument edit <subcommand> ...\n";
  std::cout << "Subcommands:\n";
  std::cout << "  info <instrument>                         - Show detailed instrument state\n";
  std::cout << "  list                                      - List editable parameter names\n";
  std::cout << "  set <instrument> <param> <value>          - Set arbitrary plugin parameter\n";
  std::cout << "  get <instrument> <param>                  - Read arbitrary plugin parameter\n";
  std::cout << "  gain <instrument> <0.0-2.0>               - Set gain\n";
  std::cout << "  attack <instrument> <ms>                  - Set attack_ms\n";
  std::cout << "  release <instrument> <ms>                 - Set release_ms\n";
  std::cout << "  root <instrument> <0-127>                 - Set sample_root\n";
  std::cout << "  pan <instrument> <L|C|R|0x##|0-255|0.0-1.0> - Set pan\n";
  std::cout << "  loop <instrument> off|on|bidi|sustain [start] [end] - Set sample loop\n";
}

void printInstrumentInfo(PluginHost& plugins, int instrument) {
  const auto instrumentIndex = static_cast<std::uint8_t>(instrument);
  if (!plugins.hasInstrumentAssignment(instrumentIndex)) {
    std::cout << "Instrument " << instrument << " has no assigned plugin\n";
    return;
  }

  const std::string pluginId = plugins.pluginForInstrument(instrumentIndex);
  const int sampleSlot = plugins.sampleSlotForInstrument(instrumentIndex);

  std::cout << "Instrument " << instrument << ": " << pluginId << "\n";
  if (sampleSlot >= 0) {
    std::cout << "  Sample slot: " << sampleSlot << "\n";
  }

  const double gain = plugins.getInstrumentParameter(instrumentIndex, "gain");
  std::cout << "  gain: " << std::fixed << std::setprecision(3) << gain << "\n";

  if (pluginId == "builtin.sample") {
    const double pan = plugins.getInstrumentParameter(instrumentIndex, "pan");
    const int root =
        static_cast<int>(plugins.getInstrumentParameter(instrumentIndex, "sample_root"));
    const int loopMode =
        static_cast<int>(plugins.getInstrumentParameter(instrumentIndex, "loop_mode"));
    const std::size_t loopStart = static_cast<std::size_t>(
        plugins.getInstrumentParameter(instrumentIndex, "loop_start"));
    const std::size_t loopEnd = static_cast<std::size_t>(
        plugins.getInstrumentParameter(instrumentIndex, "loop_end"));

    const char* loopName = "off";
    if (loopMode == 1) {
      loopName = "forward";
    } else if (loopMode == 2) {
      loopName = "bidi";
    } else if (loopMode == 3) {
      loopName = "sustain";
    }

    std::cout << "  pan: " << std::fixed << std::setprecision(3) << pan << "\n";
    std::cout << "  sample_root: " << root << "\n";
    std::cout << "  loop: " << loopName;
    if (loopMode != 0) {
      std::cout << " start=" << loopStart;
      if (loopEnd > 0) {
        std::cout << " end=" << loopEnd;
      } else {
        std::cout << " end=<sample end>";
      }
    }
    std::cout << "\n";
    return;
  }

  const double attackMs =
      plugins.getInstrumentParameter(instrumentIndex, "attack_ms");
  const double releaseMs =
      plugins.getInstrumentParameter(instrumentIndex, "release_ms");
  std::cout << "  attack_ms: " << std::fixed << std::setprecision(3) << attackMs
            << "\n";
  std::cout << "  release_ms: " << std::fixed << std::setprecision(3) << releaseMs
            << "\n";
}

void handleInstrumentEditCommand(PluginHost& plugins, std::istringstream& input) {
  std::string subcommand;
  if (!(input >> subcommand)) {
    printInstrumentEditUsage();
    return;
  }

  if (subcommand == "list") {
    std::cout << "Common params: gain, attack_ms, release_ms\n";
    std::cout << "Sample params: sample_root, pan, loop_mode, loop_start, loop_end\n";
    std::cout << "LV2 params: lv2_control_in_<index>\n";
    return;
  }

  if (subcommand == "info") {
    std::string instrumentToken;
    input >> instrumentToken;
    int instrument = -1;
    if (!tryParseInstrumentToken(instrumentToken, instrument) ||
        cli::hasExtraTokens(input)) {
      std::cout << "Usage: instrument edit info <instrument:0-"
                << (PluginHost::kMaxInstrumentSlots - 1) << ">\n";
      return;
    }
    printInstrumentInfo(plugins, instrument);
    return;
  }

  if (subcommand == "set") {
    std::string instrumentToken;
    std::string paramName;
    std::string valueToken;
    input >> instrumentToken >> paramName >> valueToken;

    int instrument = -1;
    double value = 0.0;
    if (!tryParseInstrumentToken(instrumentToken, instrument) || paramName.empty() ||
        !cli::parseStrictDoubleToken(valueToken, value) || cli::hasExtraTokens(input)) {
      std::cout << "Usage: instrument edit set <instrument> <param> <value>\n";
      return;
    }

    (void)applyInstrumentParameter(plugins, instrument, paramName, value);
    return;
  }

  if (subcommand == "get") {
    std::string instrumentToken;
    std::string paramName;
    input >> instrumentToken >> paramName;

    int instrument = -1;
    if (!tryParseInstrumentToken(instrumentToken, instrument) || paramName.empty() ||
        cli::hasExtraTokens(input)) {
      std::cout << "Usage: instrument edit get <instrument> <param>\n";
      return;
    }

    const auto instrumentIndex = static_cast<std::uint8_t>(instrument);
    if (!plugins.hasInstrumentAssignment(instrumentIndex)) {
      std::cout << "Instrument " << instrument << " has no assigned plugin\n";
      return;
    }

    const double value =
        plugins.getInstrumentParameter(instrumentIndex, paramName);
    std::cout << "Instrument " << instrument << " " << paramName << " = "
              << value << "\n";
    return;
  }

  std::string instrumentToken;
  std::string valueToken;
  input >> instrumentToken >> valueToken;
  int instrument = -1;
  if (!tryParseInstrumentToken(instrumentToken, instrument) || valueToken.empty()) {
    printInstrumentEditUsage();
    return;
  }

  if (subcommand == "gain" || subcommand == "attack" || subcommand == "release") {
    double value = 0.0;
    if (!cli::parseStrictDoubleToken(valueToken, value) || cli::hasExtraTokens(input)) {
      std::cout << "Usage: instrument edit " << subcommand
                << " <instrument> <value>\n";
      return;
    }

    const std::string paramName =
        (subcommand == "gain") ? "gain"
                                : (subcommand == "attack" ? "attack_ms"
                                                            : "release_ms");
    (void)applyInstrumentParameter(plugins, instrument, paramName, value);
    return;
  }

  if (subcommand == "root") {
    int rootMidiNote = -1;
    if (!cli::parseStrictIntToken(valueToken, rootMidiNote) || rootMidiNote < 0 ||
        rootMidiNote > 127 || cli::hasExtraTokens(input)) {
      std::cout << "Usage: instrument edit root <instrument> <0-127>\n";
      return;
    }

    (void)applyInstrumentParameter(
        plugins, instrument, "sample_root", static_cast<double>(rootMidiNote));
    return;
  }

  if (subcommand == "pan") {
    double panNormalized = 0.0;
    double panAsFloat = 0.0;
    if (cli::parseStrictDoubleToken(valueToken, panAsFloat) && panAsFloat >= 0.0 &&
        panAsFloat <= 1.0) {
      panNormalized = panAsFloat;
    } else {
      std::uint8_t panByte = 0;
      if (!SampleEditorUtils::isValidPanValue(valueToken, panByte)) {
        std::cout << "Usage: instrument edit pan <instrument>"
                  << " <L|C|R|0x##|0-255|0.0-1.0>\n";
        return;
      }
      panNormalized = static_cast<double>(panByte) / 255.0;
    }

    if (cli::hasExtraTokens(input)) {
      std::cout << "Usage: instrument edit pan <instrument>"
                << " <L|C|R|0x##|0-255|0.0-1.0>\n";
      return;
    }

    (void)applyInstrumentParameter(plugins, instrument, "pan", panNormalized);
    return;
  }

  if (subcommand == "loop") {
    std::string modeToken = valueToken;
    std::string startToken;
    std::string endToken;
    input >> startToken >> endToken;
    if (cli::hasExtraTokens(input)) {
      std::cout << "Usage: instrument edit loop <instrument> off|on|bidi|sustain"
                << " [start] [end]\n";
      return;
    }

    int loopMode = -1;
    if (modeToken == "off") {
      loopMode = 0;
    } else if (modeToken == "on" || modeToken == "forward") {
      loopMode = 1;
    } else if (modeToken == "bidi") {
      loopMode = 2;
    } else if (modeToken == "sustain") {
      loopMode = 3;
    }

    if (loopMode < 0) {
      std::cout << "Usage: instrument edit loop <instrument> off|on|bidi|sustain"
                << " [start] [end]\n";
      return;
    }

    if (!applyInstrumentParameter(
            plugins, instrument, "loop_mode", static_cast<double>(loopMode))) {
      return;
    }

    if (!startToken.empty()) {
      int loopStart = -1;
      if (!cli::parseStrictIntToken(startToken, loopStart) || loopStart < 0) {
        std::cout << "Invalid loop start frame\n";
        return;
      }
      if (!applyInstrumentParameter(
              plugins, instrument, "loop_start", static_cast<double>(loopStart))) {
        return;
      }
    }

    if (!endToken.empty()) {
      int loopEnd = -1;
      if (!cli::parseStrictIntToken(endToken, loopEnd) || loopEnd < 0) {
        std::cout << "Invalid loop end frame\n";
        return;
      }
      (void)applyInstrumentParameter(
          plugins, instrument, "loop_end", static_cast<double>(loopEnd));
    }
    return;
  }

  printInstrumentEditUsage();
}

}  // namespace

void handleInstrumentCommand(PluginHost& plugins, std::istringstream& input) {
  std::string subcommand;
  input >> subcommand;

  if (subcommand == "list") {
    if (cli::hasExtraTokens(input)) {
      std::cout << "Usage: instrument list\n";
      return;
    }

    std::cout << "Instrument assignments:\n";
    bool hasAny = false;
    for (int i = 0; i < static_cast<int>(PluginHost::kMaxInstrumentSlots); ++i) {
      if (plugins.hasInstrumentAssignment(static_cast<std::uint8_t>(i))) {
        printInstrumentSummaryLine(plugins, i);
        hasAny = true;
      }
    }
    if (!hasAny) {
      std::cout << "  (none)\n";
    }
    return;
  }

  if (subcommand == "status") {
    std::string instrumentToken;
    input >> instrumentToken;

    if (instrumentToken.empty()) {
      std::cout << "Instrument assignments:\n";
      bool hasAny = false;
      for (int i = 0; i < static_cast<int>(PluginHost::kMaxInstrumentSlots); ++i) {
        if (plugins.hasInstrumentAssignment(static_cast<std::uint8_t>(i))) {
          printInstrumentSummaryLine(plugins, i);
          hasAny = true;
        }
      }
      if (!hasAny) {
        std::cout << "  (none)\n";
      }
      return;
    }

    int instrument = -1;
    if (!tryParseInstrumentToken(instrumentToken, instrument) ||
        cli::hasExtraTokens(input)) {
      std::cout << "Usage: instrument status [instrument:0-"
                << (PluginHost::kMaxInstrumentSlots - 1) << "]\n";
      return;
    }

    if (!plugins.hasInstrumentAssignment(static_cast<std::uint8_t>(instrument))) {
      std::cout << "Instrument " << instrument << ": (unassigned)\n";
      return;
    }

    printInstrumentSummaryLine(plugins, instrument);
    return;
  }

  if (subcommand == "assign") {
    std::string instrumentToken;
    std::string pluginId;
    input >> instrumentToken >> pluginId;

    int instrument = -1;
    if (!tryParseInstrumentToken(instrumentToken, instrument) || pluginId.empty() ||
        cli::hasExtraTokens(input)) {
      std::cout << "Usage: instrument assign <instrument:0-"
                << (PluginHost::kMaxInstrumentSlots - 1) << "> <plugin-id>\n";
      return;
    }

    if (plugins.assignInstrument(static_cast<std::uint8_t>(instrument), pluginId)) {
      std::cout << "Assigned " << pluginId << " to instrument " << instrument
                << "\n";
    } else {
      std::cout << "Failed to assign plugin; ensure it is loaded and instrument index is valid\n";
    }
    return;
  }

  if (subcommand == "sample") {
    std::string instrumentToken;
    std::string slotToken;
    input >> instrumentToken >> slotToken;

    int instrument = -1;
    int sampleSlot = -1;
    if (!tryParseInstrumentToken(instrumentToken, instrument) ||
        !tryParseSampleSlotToken(slotToken, sampleSlot) || cli::hasExtraTokens(input)) {
      std::cout << "Usage: instrument sample <instrument:0-"
                << (PluginHost::kMaxInstrumentSlots - 1) << "> <slot:0-"
                << (PluginHost::kMaxSampleSlots - 1) << ">\n";
      return;
    }

    if (!plugins.assignSampleSlotToInstrument(static_cast<std::uint16_t>(sampleSlot),
                                               static_cast<std::uint8_t>(instrument))) {
      std::cout << "Failed to assign sample slot " << sampleSlot
                << " to instrument " << instrument
                << " (ensure the sample slot is loaded)\n";
      return;
    }

    std::cout << "Assigned sample slot " << sampleSlot << " to instrument "
              << instrument << "\n";
    return;
  }

  if (subcommand == "edit") {
    handleInstrumentEditCommand(plugins, input);
    return;
  }

  std::cout << "Usage: instrument <list|status|assign|sample|edit> ...\n";
  std::cout << "  instrument list                            list assigned instruments\n";
  std::cout << "  instrument status [instrument]             status of one or all instruments\n";
  std::cout << "  instrument assign <instrument> <plugin-id> assign loaded plugin\n";
  std::cout << "  instrument sample <instrument> <slot>      route sample slot to instrument\n";
  std::cout << "  instrument edit ...                        edit instrument parameters\n";
}

}  // namespace extracker
