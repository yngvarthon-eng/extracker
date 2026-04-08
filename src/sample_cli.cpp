#include "extracker/sample_cli.hpp"

#include "extracker/cli_parse_utils.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace extracker {

namespace {

bool tryParseSampleSlot(const std::string& token, int& outSlot) {
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

}  // namespace

void handleSampleCommand(PluginHost& plugins, std::istringstream& input) {
  std::string subcommand;
  input >> subcommand;

  if (subcommand == "load") {
    // sample load <slot> <name> <wav-file>
    std::string slotToken;
    std::string name;
    std::string wavPath;
    input >> slotToken >> name >> wavPath;

    int slot = -1;
    if (!tryParseSampleSlot(slotToken, slot) || name.empty() || wavPath.empty() ||
        cli::hasExtraTokens(input)) {
      std::cout << "Usage: sample load <slot:0-" << (PluginHost::kMaxSampleSlots - 1)
                << "> <name> <wav-file>\n";
      return;
    }

    if (!plugins.loadSampleToSlot(static_cast<std::uint16_t>(slot), wavPath)) {
      std::cout << "Failed to load sample: " << wavPath << "\n";
      return;
    }
    plugins.setSampleNameForSlot(static_cast<std::uint16_t>(slot), name);

    std::cout << "Sample slot " << slot << " \"" << name << "\": " << wavPath << "\n";
    return;
  }

  if (subcommand == "unload") {
    // sample unload <slot>
    std::string slotToken;
    input >> slotToken;

    int slot = -1;
    if (!tryParseSampleSlot(slotToken, slot) || cli::hasExtraTokens(input)) {
      std::cout << "Usage: sample unload <slot:0-" << (PluginHost::kMaxSampleSlots - 1) << ">\n";
      return;
    }

    const std::string oldName = plugins.sampleNameForSlot(static_cast<std::uint16_t>(slot));
    plugins.clearSampleSlot(static_cast<std::uint16_t>(slot));

    if (!oldName.empty()) {
      std::cout << "Unloaded sample slot " << slot << " (\"" << oldName << "\")\n";
    } else {
      std::cout << "Unloaded sample slot " << slot << "\n";
    }
    return;
  }

  if (subcommand == "rename") {
    // sample rename <slot> <new-name>
    std::string slotToken;
    std::string newName;
    input >> slotToken >> newName;

    int slot = -1;
    if (!tryParseSampleSlot(slotToken, slot) || newName.empty() || cli::hasExtraTokens(input)) {
      std::cout << "Usage: sample rename <slot:0-" << (PluginHost::kMaxSampleSlots - 1)
                << "> <name>\n";
      return;
    }

    if (plugins.samplePathForSlot(static_cast<std::uint16_t>(slot)).empty()) {
      std::cout << "Sample slot " << slot << " is empty\n";
      return;
    }

    plugins.setSampleNameForSlot(static_cast<std::uint16_t>(slot), newName);
    std::cout << "Renamed sample slot " << slot << " to \"" << newName << "\"\n";
    return;
  }

  if (subcommand == "play") {
    std::string slotToken;
    int note = 60;
    input >> slotToken;
    if (!(input >> note)) {
      input.clear();
    }

    int slot = -1;
    if (!tryParseSampleSlot(slotToken, slot) || note < 0 || note > 127 || cli::hasExtraTokens(input)) {
      std::cout << "Usage: sample play <slot:0-" << (PluginHost::kMaxSampleSlots - 1) << "> [note:0-127]\n";
      return;
    }

    if (slot > 255) {
      std::cout << "Sample preview currently supports slots 0-255\n";
      return;
    }
    if (plugins.samplePathForSlot(static_cast<std::uint16_t>(slot)).empty()) {
      std::cout << "Sample slot " << slot << " is empty\n";
      return;
    }
    if (!plugins.triggerNoteOn(static_cast<std::uint8_t>(slot), note, 127, true)) {
      std::cout << "Failed to preview sample slot " << slot << "\n";
      return;
    }
    std::cout << "Previewing sample slot " << slot << " at MIDI note " << note << "\n";
    return;
  }

  if (subcommand == "stop") {
    std::string slotToken;
    int note = 60;
    input >> slotToken;
    if (!(input >> note)) {
      input.clear();
    }

    int slot = -1;
    if (!tryParseSampleSlot(slotToken, slot) || note < 0 || note > 127 || cli::hasExtraTokens(input)) {
      std::cout << "Usage: sample stop <slot:0-" << (PluginHost::kMaxSampleSlots - 1) << "> [note:0-127]\n";
      return;
    }

    if (slot > 255) {
      std::cout << "Sample preview currently supports slots 0-255\n";
      return;
    }
    if (!plugins.triggerNoteOff(static_cast<std::uint8_t>(slot), note)) {
      std::cout << "Failed to stop sample slot " << slot << "\n";
      return;
    }
    std::cout << "Stopped sample slot " << slot << " at MIDI note " << note << "\n";
    return;
  }

  if (subcommand == "list" || subcommand == "status") {
    std::string slotToken;
    input >> slotToken;

    if (!slotToken.empty()) {
      // Single slot query
      int slot = -1;
      if (!tryParseSampleSlot(slotToken, slot) || cli::hasExtraTokens(input)) {
        std::cout << "Usage: sample status [slot:0-" << (PluginHost::kMaxSampleSlots - 1) << "]\n";
        return;
      }
      const std::string path = plugins.samplePathForSlot(static_cast<std::uint16_t>(slot));
      const std::string name = plugins.sampleNameForSlot(static_cast<std::uint16_t>(slot));
      if (path.empty()) {
        std::cout << "Sample slot " << slot << ": empty\n";
      } else {
        std::cout << "Sample slot " << slot << ": \"" << name << "\" -> " << path << "\n";
      }
      return;
    }

    // Full listing
    bool hasAny = false;
    std::cout << "Sample bank:\n";
    for (std::size_t i = 0; i < PluginHost::kMaxSampleSlots; ++i) {
      const std::string path = plugins.samplePathForSlot(static_cast<std::uint16_t>(i));
      if (!path.empty()) {
        const std::string name = plugins.sampleNameForSlot(static_cast<std::uint16_t>(i));
        std::cout << "  [" << i << "] \"" << name << "\" -> " << path << "\n";
        hasAny = true;
      }
    }
    if (!hasAny) {
      std::cout << "  (none loaded)\n";
    }
    return;
  }

  std::cout << "Usage: sample <load|unload|rename|play|stop|list|status> ...\n";
  std::cout << "  sample load <slot> <name> <wav-file>   load a WAV, give it a name\n";
  std::cout << "  sample unload <slot>                   unload a sample slot\n";
  std::cout << "  sample rename <slot> <name>            rename a loaded sample\n";
  std::cout << "  sample play <slot> [note]              preview a loaded sample slot\n";
  std::cout << "  sample stop <slot> [note]              stop a sample preview note\n";
  std::cout << "  sample list                            list all loaded samples\n";
  std::cout << "  sample status [slot]                   show status of one or all slots\n";
}

}  // namespace extracker
