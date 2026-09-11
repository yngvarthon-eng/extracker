#include "extracker/sequencer.hpp"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

namespace extracker {

// channel vol [<ch>] [<0-100>]
//   channel vol         — show all channel volumes
//   channel vol <ch>    — show volume for one channel
//   channel vol <ch> <0-100>  — set volume (100 = unity, 0 = silent, 50 = half)
void handleChannelCommand(std::istringstream& input, Sequencer& sequencer,
                          std::size_t numChannels) {
  std::string subcommand;
  input >> subcommand;

  if (subcommand.empty() || subcommand == "help") {
    std::cout << "channel vol [<ch>] [<0-100>]  set/show per-channel volume (100 = unity)\n";
    std::cout << "  ch vol         show all channel volumes\n";
    std::cout << "  ch vol <ch>    show volume for channel <ch>\n";
    std::cout << "  ch vol <ch> <0-100>  set channel volume\n";
    return;
  }

  if (subcommand == "vol" || subcommand == "volume") {
    std::string chToken;
    input >> chToken;

    if (chToken.empty()) {
      // Show all channels
      std::cout << "Channel volumes:\n";
      for (std::size_t ch = 0; ch < numChannels; ++ch) {
        const int pct = static_cast<int>(std::lround(sequencer.channelVolume(ch) * 100.0f));
        std::cout << "  ch " << std::setw(2) << ch << ": " << pct << "%\n";
      }
      return;
    }

    int ch = -1;
    try { ch = std::stoi(chToken); } catch (...) {}
    if (ch < 0 || static_cast<std::size_t>(ch) >= numChannels) {
      std::cout << "Invalid channel index: " << chToken
                << " (valid: 0-" << (numChannels - 1) << ")\n";
      return;
    }

    std::string valToken;
    input >> valToken;

    if (valToken.empty()) {
      // Show one channel
      const int pct = static_cast<int>(std::lround(sequencer.channelVolume(static_cast<std::size_t>(ch)) * 100.0f));
      std::cout << "Channel " << ch << " volume: " << pct << "%\n";
      return;
    }

    int pct = -1;
    try { pct = std::stoi(valToken); } catch (...) {}
    if (pct < 0 || pct > 200) {
      std::cout << "Volume out of range: " << valToken << " (valid: 0-200)\n";
      return;
    }

    sequencer.setChannelVolume(static_cast<std::size_t>(ch),
                               static_cast<float>(pct) / 100.0f);
    std::cout << "Channel " << ch << " volume set to " << pct << "%\n";
    return;
  }

  std::cout << "Unknown channel subcommand: " << subcommand << "\n";
  std::cout << "Usage: channel vol [<ch>] [<0-200>]\n";
}

}  // namespace extracker
