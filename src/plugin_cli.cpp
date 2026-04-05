#include "extracker/plugin_cli.hpp"

#include "extracker/cli_parse_utils.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace extracker {

namespace {

bool tryParseControlIndex(const std::string& token, int& outIndex) {
  if (token.empty()) {
    return false;
  }
  char* end = nullptr;
  const long value = std::strtol(token.c_str(), &end, 10);
  if (end == nullptr || *end != '\0' || value < 0) {
    return false;
  }
  outIndex = static_cast<int>(value);
  return true;
}

bool tryParseInstrumentToken(const std::string& token, int& outInstrument) {
  if (token.empty()) {
    return false;
  }
  char* end = nullptr;
  const long value = std::strtol(token.c_str(), &end, 10);
  if (end == nullptr || *end != '\0' || value < 0 || value > 255) {
    return false;
  }
  outInstrument = static_cast<int>(value);
  return true;
}

const PluginControlPortMeta* findControlMetaByToken(
    const std::vector<PluginControlPortMeta>& controls,
    const std::string& token,
    std::size_t& ordinalOut) {
  int controlIndex = -1;
  if (tryParseControlIndex(token, controlIndex)) {
    for (std::size_t index = 0; index < controls.size(); ++index) {
      if (controls[index].index == controlIndex) {
        ordinalOut = index;
        return &controls[index];
      }
    }
  }

  for (std::size_t index = 0; index < controls.size(); ++index) {
    if (controls[index].symbol == token || controls[index].label == token) {
      ordinalOut = index;
      return &controls[index];
    }
  }

  return nullptr;
}

std::string describeControlPort(const PluginControlPortMeta& meta) {
  std::string description = std::to_string(meta.index);
  if (!meta.symbol.empty()) {
    description += " [" + meta.symbol + "]";
  }
  if (!meta.label.empty()) {
    description += " \"" + meta.label + "\"";
  }
  return description;
}

}  // namespace

void handlePluginCommand(PluginHost& plugins, std::istringstream& pluginInput) {
  std::string subcommand;
  pluginInput >> subcommand;
  if (subcommand == "list") {
    if (cli::hasExtraTokens(pluginInput)) {
      std::cout << "Usage: plugin list" << '\n';
      return;
    }
    const auto available = plugins.discoverAvailablePlugins();
    if (available.empty()) {
      std::cout << "No plugins discovered" << '\n';
    } else {
      std::cout << "Discovered plugins:" << '\n';
      for (const auto& id : available) {
        std::cout << "  " << id << '\n';
      }
    }
  } else if (subcommand == "load") {
    std::string pluginId;
    pluginInput >> pluginId;
    if (pluginId.empty() || cli::hasExtraTokens(pluginInput)) {
      std::cout << "Usage: plugin load <id>" << '\n';
    } else if (plugins.loadPlugin(pluginId)) {
      std::cout << "Loaded plugin: " << pluginId << '\n';
    } else {
      std::cout << "Failed to load plugin: " << pluginId << '\n';
    }
  } else if (subcommand == "assign") {
    std::string instrumentToken;
    std::string pluginId;
    pluginInput >> instrumentToken >> pluginId;

    int instrument = -1;
    if (!tryParseInstrumentToken(instrumentToken, instrument) ||
      pluginId.empty() ||
        cli::hasExtraTokens(pluginInput)) {
      std::cout << "Usage: plugin assign <instrument> <id>" << '\n';
    } else if (plugins.assignInstrument(static_cast<std::uint8_t>(instrument), pluginId)) {
      std::cout << "Assigned " << pluginId << " to instrument " << instrument << '\n';
    } else {
      std::cout << "Failed to assign plugin; ensure it is loaded and instrument index is valid" << '\n';
    }
  } else if (subcommand == "set") {
    std::string instrumentToken;
    std::string controlPortToken;
    double value = 0.0;
    pluginInput >> instrumentToken >> controlPortToken >> value;

    int instrument = -1;
    if (!tryParseInstrumentToken(instrumentToken, instrument) ||
        controlPortToken.empty() ||
        !pluginInput ||
        cli::hasExtraTokens(pluginInput)) {
      std::cout << "Usage: plugin set <instrument> <control-port-index|symbol> <value>" << '\n';
    } else if (!plugins.hasInstrumentAssignment(static_cast<std::uint8_t>(instrument))) {
      std::cout << "Failed to set plugin control; instrument " << instrument << " has no assigned plugin" << '\n';
    } else {
      const std::string pluginId = plugins.pluginForInstrument(static_cast<std::uint8_t>(instrument));
      PluginPortInfo info;
      if (!plugins.getPluginPortInfo(pluginId, info)) {
        std::cout << "Failed to set plugin control; assigned plugin has no LV2 control metadata" << '\n';
      } else {
        std::size_t controlOrdinal = info.controlInMeta.size();
        const PluginControlPortMeta* meta = findControlMetaByToken(info.controlInMeta, controlPortToken, controlOrdinal);
        if (meta == nullptr) {
          std::cout << "Unknown control input port: " << controlPortToken << '\n';
        } else {
          const std::string parameterName = "lv2_control_in_" + std::to_string(controlOrdinal);
          if (plugins.setInstrumentParameter(static_cast<std::uint8_t>(instrument), parameterName, value)) {
            std::cout << "Set instrument " << instrument << " control port "
                      << describeControlPort(*meta) << " to " << value << '\n';
          } else {
            std::cout << "Failed to set control port " << describeControlPort(*meta)
                      << " on instrument " << instrument << '\n';
          }
        }
      }
    }
  } else if (subcommand == "get") {
    std::string instrumentToken;
    std::string controlPortToken;
    pluginInput >> instrumentToken >> controlPortToken;

    int instrument = -1;
    if (!tryParseInstrumentToken(instrumentToken, instrument) ||
        controlPortToken.empty() ||
        !pluginInput ||
        cli::hasExtraTokens(pluginInput)) {
      std::cout << "Usage: plugin get <instrument> <control-port-index|symbol>" << '\n';
    } else if (!plugins.hasInstrumentAssignment(static_cast<std::uint8_t>(instrument))) {
      std::cout << "Failed to get plugin control; instrument " << instrument << " has no assigned plugin" << '\n';
    } else {
      const std::string pluginId = plugins.pluginForInstrument(static_cast<std::uint8_t>(instrument));
      PluginPortInfo info;
      if (!plugins.getPluginPortInfo(pluginId, info)) {
        std::cout << "Failed to get plugin control; assigned plugin has no LV2 control metadata" << '\n';
      } else {
        std::size_t controlOrdinal = info.controlInMeta.size();
        if (const PluginControlPortMeta* meta = findControlMetaByToken(info.controlInMeta, controlPortToken, controlOrdinal)) {
          const std::string parameterName = "lv2_control_in_" + std::to_string(controlOrdinal);
          const double value = plugins.getInstrumentParameter(static_cast<std::uint8_t>(instrument), parameterName);
          std::cout << "Instrument " << instrument << " control port "
                    << describeControlPort(*meta) << " = " << value << " (input)" << '\n';
        } else if (const PluginControlPortMeta* meta = findControlMetaByToken(info.controlOutMeta, controlPortToken, controlOrdinal)) {
          const std::string parameterName = "lv2_control_out_" + std::to_string(controlOrdinal);
          const double value = plugins.getInstrumentParameter(static_cast<std::uint8_t>(instrument), parameterName);
          std::cout << "Instrument " << instrument << " control port "
                    << describeControlPort(*meta) << " = " << value << " (output)" << '\n';
        } else {
          std::cout << "Unknown control port: " << controlPortToken << '\n';
        }
      }
    }
  } else if (subcommand == "scan") {
    if (cli::hasExtraTokens(pluginInput)) {
      std::cout << "Usage: plugin scan" << '\n';
      return;
    }
    const std::size_t found = plugins.rescanExternalPlugins();
    if (found > 0) {
      std::cout << "Scan found " << found << " new plugin(s)" << '\n';
    } else {
      std::cout << "Scan complete, no new plugins discovered" << '\n';
    }
    std::cout << plugins.discoverAvailablePlugins().size() << " plugin(s) available" << '\n';
  } else if (subcommand == "info") {
    std::string pluginId;
    pluginInput >> pluginId;
    if (pluginId.empty() || cli::hasExtraTokens(pluginInput)) {
      std::cout << "Usage: plugin info <id>" << '\n';
    } else {
      PluginPortInfo info;
      if (!plugins.getPluginPortInfo(pluginId, info)) {
        std::cout << "Unknown plugin: " << pluginId << '\n';
      } else {
        std::cout << "Plugin: " << pluginId << '\n';
        std::cout << "  audio in:     " << info.audioIn << '\n';
        std::cout << "  audio out:    " << info.audioOut << '\n';
        std::cout << "  control in:   " << info.controlInCount << '\n';
        for (const auto& meta : info.controlInMeta) {
          std::cout << "    port " << describeControlPort(meta) << ":";
          if (meta.hasMin)     { std::cout << " min=" << meta.minVal; }
          if (meta.hasMax)     { std::cout << " max=" << meta.maxVal; }
          if (meta.hasDefault) { std::cout << " default=" << meta.defaultVal; }
          if (!meta.hasMin && !meta.hasMax && !meta.hasDefault) { std::cout << " (no range)"; }
          std::cout << '\n';
        }
        std::cout << "  control out:  " << info.controlOutCount << '\n';
        for (const auto& meta : info.controlOutMeta) {
          std::cout << "    port " << describeControlPort(meta) << ":";
          if (!meta.hasMin && !meta.hasMax && !meta.hasDefault) {
            std::cout << " (no range)";
          } else {
            if (meta.hasMin)     { std::cout << " min=" << meta.minVal; }
            if (meta.hasMax)     { std::cout << " max=" << meta.maxVal; }
            if (meta.hasDefault) { std::cout << " default=" << meta.defaultVal; }
          }
          std::cout << '\n';
        }
        std::cout << "  event in:     " << info.eventInCount << '\n';
      }
    }
  } else if (subcommand == "status") {
    if (cli::hasExtraTokens(pluginInput)) {
      std::cout << "Usage: plugin status" << '\n';
      return;
    }
    std::cout << "Instrument assignments:" << '\n';
    bool hasAny = false;
    for (std::size_t i = 0; i < PluginHost::kMaxInstrumentSlots; ++i) {
      if (plugins.hasInstrumentAssignment(static_cast<std::uint8_t>(i))) {
        std::cout << "  " << i << ": "
                  << plugins.pluginForInstrument(static_cast<std::uint8_t>(i)) << '\n';
        hasAny = true;
      }
    }
    if (!hasAny) {
      std::cout << "  (none)" << '\n';
    }
  } else {
    std::cout << "Usage: plugin <scan|list|load|assign|set|get|info|status> ..." << '\n';
  }
}

void handleSineCommand(PluginHost& plugins, std::istringstream& sineInput) {
  std::string instrumentToken;
  sineInput >> instrumentToken;

  int instrument = -1;
  if (!tryParseInstrumentToken(instrumentToken, instrument) || cli::hasExtraTokens(sineInput)) {
    std::cout << "Usage: sine <instrument>" << '\n';
  } else {
    if (!plugins.loadPlugin("builtin.sine")) {
      std::cout << "Failed to load builtin.sine" << '\n';
    } else if (!plugins.assignInstrument(static_cast<std::uint8_t>(instrument), "builtin.sine")) {
      std::cout << "Failed to assign builtin.sine to instrument " << instrument << '\n';
    } else {
      std::cout << "Assigned builtin.sine to instrument " << instrument << '\n';
    }
  }
}

void handleHelpCommand() {
  std::cout << "play                       start playback" << '\n';
  std::cout << "stop                       stop playback" << '\n';
  std::cout << "tempo <bpm>                set tempo" << '\n';
  std::cout << "loop <on|off>              enable/disable looping for active play range" << '\n';
  std::cout << "loop range <from> <to>     define loop/play range without starting" << '\n';
  std::cout << "status                     show engine/transport/plugin state" << '\n';
  std::cout << "reset                      stop playback and reset counters" << '\n';
  std::cout << "save <file>                save module to file (defaults to .ex)" << '\n';
  std::cout << "load <file>                load module from file (defaults to .ex)" << '\n';
  std::cout << "plugin scan                rescan LV2 paths for available plugins" << '\n';
  std::cout << "plugin list                list discovered plugins" << '\n';
  std::cout << "plugin load <id>           load plugin by id (e.g. builtin.sine)" << '\n';
  std::cout << "plugin assign <i> <id>     assign loaded plugin to instrument slot" << '\n';
  std::cout << "plugin set <i> <p> <v>     set LV2 control input by port index or symbol" << '\n';
  std::cout << "plugin get <i> <p>         get LV2 control by port index or symbol" << '\n';
  std::cout << "plugin info <id>           show port layout for a plugin" << '\n';
  std::cout << "plugin status              show instrument->plugin assignments" << '\n';
  std::cout << "sine <instrument>          convenience command for builtin.sine" << '\n';
  std::cout << "note set r c n i [v fx fv] set note in pattern (optional vel/effect)" << '\n';
  std::cout << "note set dry ...           parse and preview note set without writing" << '\n';
  std::cout << "note clear dry r c         preview note clear without writing" << '\n';
  std::cout << "note clear r c             clear note at row/channel" << '\n';
  std::cout << "note vel dry r c v         preview velocity set without writing" << '\n';
  std::cout << "note vel r c v             set velocity for existing step" << '\n';
  std::cout << "note gate dry r c t        preview gate tick set without writing" << '\n';
  std::cout << "note gate r c t            set gate ticks for existing step" << '\n';
  std::cout << "note fx dry r c f fv       preview effect set without writing" << '\n';
  std::cout << "note fx r c f fv           set effect command/value for step" << '\n';
  std::cout << "pattern print [from] [to]  print pattern rows (default 0..15)" << '\n';
  std::cout << "pattern play [f] [t]       play selected row range, or full pattern" << '\n';
  std::cout << "pattern template <name>    load a starter groove template" << '\n';
  std::cout << "pattern transpose [dry [preview [verbose]]] s [f t c] [step n] transpose notes by semitones" << '\n';
  std::cout << "pattern velocity [dry [preview [verbose]]] p [f t c] [step n] scale velocity by percent" << '\n';
  std::cout << "pattern gate [dry [preview [verbose]]] p [f t c] [step n] scale gate ticks by percent" << '\n';
  std::cout << "pattern effect [dry [preview [verbose]]] fx fv [f t c] [step n] fill effect command/value" << '\n';
  std::cout << "pattern copy f t [cf ct] [step n] copy range into internal pattern clipboard" << '\n';
  std::cout << "pattern paste [dry ...] r [offset] [step n] paste clipboard at target row/channel offset" << '\n';
  std::cout << "pattern humanize [dry ...] vr gr seed [f t c] [step n] randomize velocity/gate around current values" << '\n';
  std::cout << "pattern randomize [dry ...] p seed [f t c] [step n] randomize velocity/effect per-step by probability" << '\n';
  std::cout << "pattern undo                undo last committed bulk pattern edit" << '\n';
  std::cout << "pattern redo                redo last undone bulk pattern edit" << '\n';
  std::cout << "record on [channel]        arm step recording" << '\n';
  std::cout << "record off                 disarm step recording" << '\n';
  std::cout << "record channel <...>       set/show record channel without re-arming" << '\n';
  std::cout << "record cursor <...>        set/show/move record cursor row (e.g. 12, +4, -1, start, end, next, prev)" << '\n';
  std::cout << "record note ...            write to next empty row in armed channel (supports positional and keyword forms; instr defaults to midi instrument)" << '\n';
  std::cout << "record note dry ...        parse and preview target row/value without writing" << '\n';
  std::cout << "record quantize <...>      use current transport row when recording" << '\n';
  std::cout << "record overdub <...>       allow replacing notes on occupied steps" << '\n';
  std::cout << "record jump <ticks|ratio|status> cursor jump after each inserted note (e.g. 8, 1/1, 3/2)" << '\n';
  std::cout << "record undo                revert the last recorded step write" << '\n';
  std::cout << "record redo                re-apply the last undone record write" << '\n';
  std::cout << "midi on                    start ALSA MIDI input" << '\n';
  std::cout << "midi off                   stop ALSA MIDI input" << '\n';
  std::cout << "midi status                show MIDI backend/connection hint" << '\n';
  std::cout << "midi quick [all|compact]   compact MIDI summary (single or composed views)" << '\n';
  std::cout << "midi thru <on|off>         enable/disable live MIDI audition" << '\n';
  std::cout << "midi instrument <index>    set instrument slot for MIDI thru/record" << '\n';
  std::cout << "midi learn <on|off|status> auto-map incoming MIDI channels to instruments" << '\n';
  std::cout << "midi map <channel> <instr> set fixed channel->instrument mapping" << '\n';
  std::cout << "midi map status            show all active channel->instrument mappings" << '\n';
  std::cout << "midi map <channel> clear   clear one channel mapping" << '\n';
  std::cout << "midi map clear all         clear all channel mappings" << '\n';
  std::cout << "midi transport <...>       sync transport from MIDI start/stop/clock" << '\n';
  std::cout << "midi transport timeout <ms|status> set/show MIDI clock timeout" << '\n';
  std::cout << "midi transport lock <on|off|status> lock fallback tempo to external BPM" << '\n';
  std::cout << "midi clock help            print ALSA MIDI clock setup tips" << '\n';
  std::cout << "midi clock sources [name]  list matching MIDI clock source ports" << '\n';
  std::cout << "midi clock autoconnect [name] [index] auto-connect virtual MIDI clock source" << '\n';
  std::cout << "midi clock diagnose [name] quick routing diagnostics for clock source" << '\n';
  std::cout << "midi clock diagnose live [name] live clock health probe (1s)" << '\n';
  std::cout << "quit                       exit" << '\n';
}

}  // namespace extracker
