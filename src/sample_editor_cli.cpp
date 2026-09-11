#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include "extracker/plugin_host.hpp"
#include "extracker/sample_editor_utils.hpp"

namespace extracker {

void handleSampleEditCommand(PluginHost& plugins, std::istringstream& input) {
  std::string subcommand;
  if (!(input >> subcommand)) {
    std::cout << "Usage: sample edit <subcommand> [options]\n";
    std::cout << "Subcommands:\n";
    std::cout << "  trim <slot> [dry] --from <val> --to <val> [--unit s|f]  - Trim sample\n";
    std::cout << "  normalize <slot> [dry] [--from <val>] [--to <val>] [--unit s|f] - Normalize\n";
    std::cout << "  fade <slot> in|out [dry] [--from <val>] [--to <val>] [--unit s|f] - Fade in/out\n";
    std::cout << "  reverse <slot> [dry] [--from <val>] [--to <val>] [--unit s|f] - Reverse sample\n";
    std::cout << "  resample <slot> [dry] (--rate <hz> | --factor <x>) - Resample (Lanczos, pitch-preserving)\n";
    std::cout << "  bitdepth <slot> [dry] <bits> [--from <val>] [--to <val>] [--unit s|f] - Reduce bit depth\n";
    std::cout << "  loop-crossfade <slot> [dry] [--len <val>] [--unit s|f] - Crossfade loop seam (set loop first)\n";
    std::cout << "  restore <slot> [dry] - Restore original sample\n";
    std::cout << "  loop <slot> on|off|bidi|sustain [--start <val>] [--end <val>] [--unit s|f] - Loop config\n";
    std::cout << "  pan <slot> <L|C|R|0x##|0-255> - Set pan\n";
    std::cout << "  volume <slot> <0.0-2.0> - Set volume multiplier\n";
    std::cout << "  transpose <slot> <semitones> - Set root note offset from C4\n";
    std::cout << "  info <slot> - Show sample metadata\n";
    std::cout << "  list - List all samples\n";
    return;
  }

  // 'list' has no slot argument
  if (subcommand == "list") {
    int count = 0;
    for (int i = 0; i < static_cast<int>(PluginHost::kMaxSampleSlots); ++i) {
      auto name = plugins.sampleNameForSlot(static_cast<std::uint16_t>(i));
      auto frameCount = plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(i));
      if (frameCount > 0) {
        auto sampleRate = plugins.sampleRateForSlot(static_cast<std::uint16_t>(i));
        if (sampleRate == 0) sampleRate = 44100;
        double durationSec = SampleEditorUtils::framesToSeconds(frameCount, sampleRate);
        std::cout << "  Slot " << std::setw(3) << i << ": " << name << " ("
                  << std::fixed << std::setprecision(3) << durationSec << "s, " << frameCount << " frames)\n";
        count++;
      }
    }
    std::cout << count << " sample(s) loaded\n";
    return;
  }

  std::string slotStr;
  if (!(input >> slotStr)) {
    std::cout << "Usage: sample edit " << subcommand << " <slot> [options]\n";
    return;
  }

  // Parse slot number
  int slot = -1;
  try {
    slot = std::stoi(slotStr);
  } catch (...) {
    std::cout << "Invalid slot number: " << slotStr << "\n";
    return;
  }

  if (slot < 0 || slot >= static_cast<int>(PluginHost::kMaxSampleSlots)) {
    std::cout << "Slot out of range (0-" << (PluginHost::kMaxSampleSlots - 1) << ")\n";
    return;
  }

  // ── info ──────────────────────────────────────────────────────────────────
  if (subcommand == "info") {
    auto frameCount = plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(slot));
    auto sampleRate = plugins.sampleRateForSlot(static_cast<std::uint16_t>(slot));
    auto path = plugins.samplePathForSlot(static_cast<std::uint16_t>(slot));
    auto name = plugins.sampleNameForSlot(static_cast<std::uint16_t>(slot));

    if (frameCount == 0) {
      std::cout << "Sample slot " << slot << " is empty\n";
      return;
    }

    if (sampleRate == 0) sampleRate = 44100;
    double durationSec = SampleEditorUtils::framesToSeconds(frameCount, sampleRate);

    const double gainVal = plugins.getSampleSlotParameter(static_cast<std::uint16_t>(slot), "gain");
    const double panVal  = plugins.getSampleSlotParameter(static_cast<std::uint16_t>(slot), "pan");
    const int rootNote   = static_cast<int>(plugins.getSampleSlotParameter(static_cast<std::uint16_t>(slot), "sample_root"));
    const int loopMode   = static_cast<int>(plugins.getSampleSlotParameter(static_cast<std::uint16_t>(slot), "loop_mode"));
    const std::size_t loopStart = static_cast<std::size_t>(plugins.getSampleSlotParameter(static_cast<std::uint16_t>(slot), "loop_start"));
    const double loopEndRaw = plugins.getSampleSlotParameter(static_cast<std::uint16_t>(slot), "loop_end");
    const bool hasLoopEnd = loopEndRaw > 0.0;

    const char* loopNames[] = { "off", "forward", "bidi", "sustain" };
    const char* loopName = (loopMode >= 0 && loopMode <= 3) ? loopNames[loopMode] : "unknown";

    std::cout << "Sample slot " << slot << ": " << name << "\n";
    std::cout << "  Path: " << path << "\n";
    std::cout << "  Sample rate: " << sampleRate << " Hz\n";
    std::cout << "  Frames: " << frameCount << " (" << std::fixed << std::setprecision(3) << durationSec << "s)\n";
    std::cout << "  Volume: " << std::fixed << std::setprecision(2) << gainVal << "\n";
    std::cout << "  Pan: 0x" << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(static_cast<std::uint8_t>(panVal * 255.0 + 0.5))
              << std::dec << std::setfill(' ') << " (" << std::fixed << std::setprecision(2) << panVal << ")\n";
    std::cout << "  Root note: " << rootNote << " (C4=60, offset " << (rootNote - 60) << " semitones)\n";
    std::cout << "  Loop: " << loopName;
    if (loopMode != 0) {
      std::cout << " start=" << loopStart;
      if (hasLoopEnd) {
        std::cout << " end=" << static_cast<std::size_t>(loopEndRaw);
      } else {
        std::cout << " end=<sample end>";
      }
    }
    std::cout << "\n";
    return;
  }

  // ── trim ──────────────────────────────────────────────────────────────────
  if (subcommand == "trim") {
    bool dryRun = false;
    std::string token;
    if (input >> std::ws && input.peek() != EOF) {
      std::streampos pos = input.tellg();
      if (input >> token && token == "dry") {
        dryRun = true;
      } else {
        input.seekg(pos);
      }
    }

    std::string unit = "s";
    double fromVal = 0, toVal = 0;
    bool hasFrom = false, hasTo = false;

    while (input >> token) {
      if (token == "--from" && input >> token) {
        try { fromVal = std::stod(token); hasFrom = true; }
        catch (...) { std::cout << "Invalid --from value\n"; return; }
      } else if (token == "--to" && input >> token) {
        try { toVal = std::stod(token); hasTo = true; }
        catch (...) { std::cout << "Invalid --to value\n"; return; }
      } else if (token == "--unit" && input >> token) {
        if (token == "s" || token == "f") { unit = token; }
        else { std::cout << "Invalid --unit (use 's' or 'f')\n"; return; }
      } else {
        std::cout << "Unknown option: " << token << "\n"; return;
      }
    }

    if (!hasFrom || !hasTo) {
      std::cout << "Usage: sample edit trim <slot> [dry] --from <val> --to <val> [--unit s|f]\n";
      return;
    }

    auto frameCount = plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(slot));
    if (frameCount == 0) { std::cout << "Sample slot " << slot << " is empty\n"; return; }

    auto sampleRate = plugins.sampleRateForSlot(static_cast<std::uint16_t>(slot));
    if (sampleRate == 0) sampleRate = 44100;

    std::size_t startFrame = (unit == "s") ?
      SampleEditorUtils::secondsToFrames(fromVal, sampleRate) : static_cast<std::size_t>(fromVal);
    std::size_t endFrame = (unit == "s") ?
      SampleEditorUtils::secondsToFrames(toVal, sampleRate) : static_cast<std::size_t>(toVal);

    if (!SampleEditorUtils::isValidFrameRange(startFrame, endFrame, frameCount)) {
      std::cout << "Invalid frame range\n"; return;
    }

    if (dryRun) {
      double startSec = SampleEditorUtils::framesToSeconds(startFrame, sampleRate);
      double endSec = SampleEditorUtils::framesToSeconds(endFrame, sampleRate);
      std::cout << "Trim dry-run: slot " << slot << ", frames " << startFrame << "-" << endFrame
                << " (" << std::fixed << std::setprecision(2) << startSec << "s-" << endSec << "s), "
                << "keeps " << (endFrame - startFrame) << " frames\n";
    } else {
      plugins.trimSampleSlot(static_cast<std::uint16_t>(slot), startFrame, endFrame);
      std::cout << "Trimmed slot " << slot << " to " << (endFrame - startFrame) << " frames\n";
    }
    return;
  }

  // ── normalize ─────────────────────────────────────────────────────────────
  if (subcommand == "normalize") {
    bool dryRun = false;
    std::string token;
    if (input >> std::ws && input.peek() != EOF) {
      std::streampos pos = input.tellg();
      if (input >> token && token == "dry") {
        dryRun = true;
      } else {
        input.seekg(pos);
      }
    }

    auto frameCount = plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(slot));
    if (frameCount == 0) { std::cout << "Sample slot " << slot << " is empty\n"; return; }

    if (dryRun) {
      std::cout << "Normalize dry-run: slot " << slot << ", " << frameCount << " frames\n";
    } else {
      plugins.normalizeSampleSlot(static_cast<std::uint16_t>(slot), 0, frameCount);
      std::cout << "Normalized slot " << slot << "\n";
    }
    return;
  }

  // ── fade ──────────────────────────────────────────────────────────────────
  if (subcommand == "fade") {
    std::string direction;
    if (!(input >> direction) || (direction != "in" && direction != "out")) {
      std::cout << "Usage: sample edit fade <slot> in|out [dry] [--from <val>] [--to <val>] [--unit s|f]\n";
      return;
    }

    bool dryRun = false;
    std::string token;
    if (input >> std::ws && input.peek() != EOF) {
      std::streampos pos = input.tellg();
      if (input >> token && token == "dry") {
        dryRun = true;
      } else {
        input.seekg(pos);
      }
    }

    auto frameCount = plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(slot));
    if (frameCount == 0) { std::cout << "Sample slot " << slot << " is empty\n"; return; }

    auto sampleRate = plugins.sampleRateForSlot(static_cast<std::uint16_t>(slot));
    if (sampleRate == 0) sampleRate = 44100;

    std::string unit = "s";
    double fromVal = -1.0, toVal = -1.0;
    bool hasFrom = false, hasTo = false;

    while (input >> token) {
      if (token == "--from" && input >> token) {
        try { fromVal = std::stod(token); hasFrom = true; }
        catch (...) { std::cout << "Invalid --from value\n"; return; }
      } else if (token == "--to" && input >> token) {
        try { toVal = std::stod(token); hasTo = true; }
        catch (...) { std::cout << "Invalid --to value\n"; return; }
      } else if (token == "--unit" && input >> token) {
        if (token == "s" || token == "f") { unit = token; }
        else { std::cout << "Invalid --unit (use 's' or 'f')\n"; return; }
      } else {
        std::cout << "Unknown option: " << token << "\n"; return;
      }
    }

    std::size_t startFrame = 0;
    std::size_t endFrame = frameCount;

    if (hasFrom) {
      startFrame = (unit == "s") ?
        SampleEditorUtils::secondsToFrames(fromVal, sampleRate) : static_cast<std::size_t>(fromVal);
    }
    if (hasTo) {
      endFrame = (unit == "s") ?
        SampleEditorUtils::secondsToFrames(toVal, sampleRate) : static_cast<std::size_t>(toVal);
    }

    if (!SampleEditorUtils::isValidFrameRange(startFrame, endFrame, frameCount)) {
      std::cout << "Invalid frame range\n"; return;
    }

    if (dryRun) {
      std::cout << "Fade " << direction << " dry-run: slot " << slot
                << ", frames " << startFrame << "-" << endFrame << "\n";
    } else {
      bool ok = (direction == "in")
          ? plugins.fadeInSampleSlot(static_cast<std::uint16_t>(slot), startFrame, endFrame)
          : plugins.fadeOutSampleSlot(static_cast<std::uint16_t>(slot), startFrame, endFrame);
      if (ok) {
        std::cout << "Fade " << direction << " applied to slot " << slot
                  << " frames " << startFrame << "-" << endFrame << "\n";
      } else {
        std::cout << "Fade " << direction << " failed\n";
      }
    }
    return;
  }

  // ── reverse ───────────────────────────────────────────────────────────────
  if (subcommand == "reverse") {
    bool dryRun = false;
    std::string token;
    if (input >> std::ws && input.peek() != EOF) {
      std::streampos pos = input.tellg();
      if (input >> token && token == "dry") {
        dryRun = true;
      } else {
        input.seekg(pos);
      }
    }

    auto frameCount = plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(slot));
    if (frameCount == 0) { std::cout << "Sample slot " << slot << " is empty\n"; return; }

    auto sampleRate = plugins.sampleRateForSlot(static_cast<std::uint16_t>(slot));
    if (sampleRate == 0) sampleRate = 44100;

    std::string unit = "s";
    double fromVal = -1.0, toVal = -1.0;
    bool hasFrom = false, hasTo = false;

    while (input >> token) {
      if (token == "--from" && input >> token) {
        try { fromVal = std::stod(token); hasFrom = true; }
        catch (...) { std::cout << "Invalid --from value\n"; return; }
      } else if (token == "--to" && input >> token) {
        try { toVal = std::stod(token); hasTo = true; }
        catch (...) { std::cout << "Invalid --to value\n"; return; }
      } else if (token == "--unit" && input >> token) {
        if (token == "s" || token == "f") { unit = token; }
        else { std::cout << "Invalid --unit (use 's' or 'f')\n"; return; }
      } else {
        std::cout << "Unknown option: " << token << "\n"; return;
      }
    }

    std::size_t startFrame = 0;
    std::size_t endFrame = frameCount;

    if (hasFrom) {
      startFrame = (unit == "s") ?
        SampleEditorUtils::secondsToFrames(fromVal, sampleRate) : static_cast<std::size_t>(fromVal);
    }
    if (hasTo) {
      endFrame = (unit == "s") ?
        SampleEditorUtils::secondsToFrames(toVal, sampleRate) : static_cast<std::size_t>(toVal);
    }

    if (!SampleEditorUtils::isValidFrameRange(startFrame, endFrame, frameCount)) {
      std::cout << "Invalid frame range\n"; return;
    }

    if (dryRun) {
      std::cout << "Reverse dry-run: slot " << slot
                << ", frames " << startFrame << "-" << endFrame
                << " (" << (endFrame - startFrame) << " frames)\n";
    } else {
      bool ok = plugins.reverseSampleSlot(static_cast<std::uint16_t>(slot), startFrame, endFrame);
      if (ok) {
        std::cout << "Reversed slot " << slot
                  << " frames " << startFrame << "-" << endFrame << "\n";
      } else {
        std::cout << "Reverse failed\n";
      }
    }
    return;
  }

  // ── resample ──────────────────────────────────────────────────────────────
  if (subcommand == "resample") {
    bool dryRun = false;
    std::string token;
    if (input >> std::ws && input.peek() != EOF) {
      std::streampos pos = input.tellg();
      if (input >> token && token == "dry") {
        dryRun = true;
      } else {
        input.seekg(pos);
      }
    }

    double rateVal = 0.0, factorVal = 0.0;
    bool hasRate = false, hasFactor = false;

    while (input >> token) {
      if (token == "--rate" && input >> token) {
        try { rateVal = std::stod(token); hasRate = true; }
        catch (...) { std::cout << "Invalid --rate value\n"; return; }
      } else if (token == "--factor" && input >> token) {
        try { factorVal = std::stod(token); hasFactor = true; }
        catch (...) { std::cout << "Invalid --factor value\n"; return; }
      } else {
        std::cout << "Unknown option: " << token << "\n"; return;
      }
    }

    if (hasRate == hasFactor) {
      std::cout << "Usage: sample edit resample <slot> [dry] (--rate <hz> | --factor <x>)\n";
      return;
    }

    auto frameCount = plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(slot));
    if (frameCount == 0) { std::cout << "Sample slot " << slot << " is empty\n"; return; }

    auto sampleRate = plugins.sampleRateForSlot(static_cast<std::uint16_t>(slot));
    if (sampleRate == 0) sampleRate = 44100;

    double targetRate = hasRate ? rateVal : (static_cast<double>(sampleRate) * factorVal);
    if (targetRate < 1000.0 || targetRate > 192000.0) {
      std::cout << "Target rate out of range (1000-192000 Hz): " << static_cast<long>(targetRate) << "\n";
      return;
    }
    auto newRate = static_cast<std::uint32_t>(targetRate + 0.5);
    if (newRate == sampleRate) {
      std::cout << "Target rate equals current rate (" << sampleRate << " Hz); nothing to do\n";
      return;
    }

    std::size_t newFrames = static_cast<std::size_t>(
        static_cast<double>(frameCount) * static_cast<double>(newRate) / static_cast<double>(sampleRate) + 0.5);

    if (dryRun) {
      std::cout << "Resample dry-run: slot " << slot << ", " << sampleRate << "Hz -> " << newRate
                << "Hz (" << frameCount << " -> ~" << newFrames << " frames)\n";
    } else {
      bool ok = plugins.resampleSampleSlot(static_cast<std::uint16_t>(slot), newRate);
      if (ok) {
        std::cout << "Resampled slot " << slot << " to " << newRate << "Hz ("
                  << plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(slot)) << " frames)\n";
      } else {
        std::cout << "Resample failed\n";
      }
    }
    return;
  }

  // ── bitdepth ──────────────────────────────────────────────────────────────
  if (subcommand == "bitdepth") {
    bool dryRun = false;
    std::string token;
    if (input >> std::ws && input.peek() != EOF) {
      std::streampos pos = input.tellg();
      if (input >> token && token == "dry") {
        dryRun = true;
      } else {
        input.seekg(pos);
      }
    }

    std::string bitsStr;
    if (!(input >> bitsStr)) {
      std::cout << "Usage: sample edit bitdepth <slot> [dry] <bits> [--from <val>] [--to <val>] [--unit s|f]\n";
      return;
    }
    int bits = 0;
    try { bits = std::stoi(bitsStr); }
    catch (...) { std::cout << "Invalid bits value\n"; return; }
    if (bits < 1 || bits > 32) {
      std::cout << "Bits out of range (1-32)\n";
      return;
    }

    auto frameCount = plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(slot));
    if (frameCount == 0) { std::cout << "Sample slot " << slot << " is empty\n"; return; }

    auto sampleRate = plugins.sampleRateForSlot(static_cast<std::uint16_t>(slot));
    if (sampleRate == 0) sampleRate = 44100;

    std::string unit = "s";
    double fromVal = -1.0, toVal = -1.0;
    bool hasFrom = false, hasTo = false;

    while (input >> token) {
      if (token == "--from" && input >> token) {
        try { fromVal = std::stod(token); hasFrom = true; }
        catch (...) { std::cout << "Invalid --from value\n"; return; }
      } else if (token == "--to" && input >> token) {
        try { toVal = std::stod(token); hasTo = true; }
        catch (...) { std::cout << "Invalid --to value\n"; return; }
      } else if (token == "--unit" && input >> token) {
        if (token == "s" || token == "f") { unit = token; }
        else { std::cout << "Invalid --unit (use 's' or 'f')\n"; return; }
      } else {
        std::cout << "Unknown option: " << token << "\n"; return;
      }
    }

    std::size_t startFrame = 0;
    std::size_t endFrame = frameCount;

    if (hasFrom) {
      startFrame = (unit == "s") ?
        SampleEditorUtils::secondsToFrames(fromVal, sampleRate) : static_cast<std::size_t>(fromVal);
    }
    if (hasTo) {
      endFrame = (unit == "s") ?
        SampleEditorUtils::secondsToFrames(toVal, sampleRate) : static_cast<std::size_t>(toVal);
    }

    if (!SampleEditorUtils::isValidFrameRange(startFrame, endFrame, frameCount)) {
      std::cout << "Invalid frame range\n"; return;
    }

    if (dryRun) {
      std::cout << "Bitdepth dry-run: slot " << slot << ", " << bits << " bits, frames "
                << startFrame << "-" << endFrame << "\n";
    } else {
      bool ok = plugins.bitDepthSampleSlot(static_cast<std::uint16_t>(slot), bits, startFrame, endFrame);
      if (ok) {
        std::cout << "Reduced slot " << slot << " to " << bits << " bits (frames "
                  << startFrame << "-" << endFrame << ")\n";
      } else {
        std::cout << "Bitdepth reduction failed\n";
      }
    }
    return;
  }

  // ── loop-crossfade ────────────────────────────────────────────────────────
  if (subcommand == "loop-crossfade") {
    bool dryRun = false;
    std::string token;
    if (input >> std::ws && input.peek() != EOF) {
      std::streampos pos = input.tellg();
      if (input >> token && token == "dry") {
        dryRun = true;
      } else {
        input.seekg(pos);
      }
    }

    std::string unit = "s";
    double lenVal = -1.0;
    bool hasLen = false;

    while (input >> token) {
      if (token == "--len" && input >> token) {
        try { lenVal = std::stod(token); hasLen = true; }
        catch (...) { std::cout << "Invalid --len value\n"; return; }
      } else if (token == "--unit" && input >> token) {
        if (token == "s" || token == "f") { unit = token; }
        else { std::cout << "Invalid --unit (use 's' or 'f')\n"; return; }
      } else {
        std::cout << "Unknown option: " << token << "\n"; return;
      }
    }

    auto frameCount = plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(slot));
    if (frameCount == 0) { std::cout << "Sample slot " << slot << " is empty\n"; return; }

    auto sampleRate = plugins.sampleRateForSlot(static_cast<std::uint16_t>(slot));
    if (sampleRate == 0) sampleRate = 44100;

    const int loopMode = static_cast<int>(plugins.getSampleSlotParameter(static_cast<std::uint16_t>(slot), "loop_mode"));
    if (loopMode != 1 && loopMode != 3) {
      std::cout << "Set a forward or sustain loop (with start>0) first: sample edit loop " << slot << " on --start <f> --end <f> --unit f\n";
      return;
    }

    const std::size_t loopStart = static_cast<std::size_t>(plugins.getSampleSlotParameter(static_cast<std::uint16_t>(slot), "loop_start"));
    const double loopEndRaw = plugins.getSampleSlotParameter(static_cast<std::uint16_t>(slot), "loop_end");
    const std::size_t loopEnd = (loopEndRaw > 0.0) ? static_cast<std::size_t>(loopEndRaw) : frameCount;
    if (loopStart < 1 || loopEnd <= loopStart) {
      std::cout << "Loop needs start>=1 and end>start for a crossfade (start=" << loopStart << " end=" << loopEnd << ")\n";
      return;
    }

    std::size_t lenFrames = hasLen
        ? ((unit == "s") ? SampleEditorUtils::secondsToFrames(lenVal, sampleRate) : static_cast<std::size_t>(lenVal))
        : 256;
    const std::size_t effLen = std::min({lenFrames, loopStart, loopEnd - loopStart});

    if (dryRun) {
      std::cout << "Loop-crossfade dry-run: slot " << slot << ", len " << effLen
                << " frames, loop " << loopStart << "-" << loopEnd << "\n";
    } else {
      bool ok = plugins.crossfadeLoopSampleSlot(static_cast<std::uint16_t>(slot), lenFrames);
      if (ok) {
        std::cout << "Crossfaded loop seam on slot " << slot << " (" << effLen
                  << " frames, loop " << loopStart << "-" << loopEnd << ")\n";
      } else {
        std::cout << "Loop-crossfade failed (check loop points)\n";
      }
    }
    return;
  }

  // ── restore ───────────────────────────────────────────────────────────────
  if (subcommand == "restore") {
    bool dryRun = false;
    std::string token;
    if (input >> token && token == "dry") {
      dryRun = true;
    }

    if (dryRun) {
      std::cout << "Restore dry-run: slot " << slot << "\n";
    } else {
      plugins.restoreSampleSlotSource(static_cast<std::uint16_t>(slot));
      std::cout << "Restored slot " << slot << " to original\n";
    }
    return;
  }

  // ── pan ───────────────────────────────────────────────────────────────────
  if (subcommand == "pan") {
    std::string panStr;
    if (!(input >> panStr)) {
      std::cout << "Usage: sample edit pan <slot> <L|C|R|0x##|0-255>\n";
      return;
    }

    std::uint8_t panByte = 0x80;
    if (!SampleEditorUtils::isValidPanValue(panStr, panByte)) {
      std::cout << "Invalid pan value (use L, C, R, 0x00-0xFF, or 0-255)\n";
      return;
    }

    if (plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(slot)) == 0) {
      std::cout << "Sample slot " << slot << " is empty\n";
      return;
    }

    const double panNorm = static_cast<double>(panByte) / 255.0;
    plugins.setSampleSlotParameter(static_cast<std::uint16_t>(slot), "pan", panNorm);
    std::cout << "Pan set to 0x" << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(panByte) << std::dec << std::setfill(' ')
              << " for slot " << slot << "\n";
    return;
  }

  // ── volume ────────────────────────────────────────────────────────────────
  if (subcommand == "volume") {
    std::string volStr;
    if (!(input >> volStr)) {
      std::cout << "Usage: sample edit volume <slot> <0.0-2.0>\n";
      return;
    }

    float volVal = 1.0f;
    if (!SampleEditorUtils::isValidVolumeValue(volStr, volVal)) {
      std::cout << "Invalid volume (use 0.1-2.0)\n";
      return;
    }

    if (plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(slot)) == 0) {
      std::cout << "Sample slot " << slot << " is empty\n";
      return;
    }

    plugins.setSampleSlotParameter(static_cast<std::uint16_t>(slot), "gain", static_cast<double>(volVal));
    std::cout << "Volume set to " << std::fixed << std::setprecision(2) << volVal
              << " for slot " << slot << "\n";
    return;
  }

  // ── transpose ─────────────────────────────────────────────────────────────
  if (subcommand == "transpose") {
    std::string semStr;
    if (!(input >> semStr)) {
      std::cout << "Usage: sample edit transpose <slot> <semitones>\n";
      return;
    }

    int semitones = 0;
    try {
      semitones = std::stoi(semStr);
    } catch (...) {
      std::cout << "Invalid semitone value\n";
      return;
    }

    const int newRoot = std::clamp(60 + semitones, 0, 127);
    if (newRoot != 60 + semitones) {
      std::cout << "Semitone offset clamped: root note " << newRoot << " (MIDI 0-127)\n";
    }

    if (plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(slot)) == 0) {
      std::cout << "Sample slot " << slot << " is empty\n";
      return;
    }

    plugins.setSampleSlotParameter(static_cast<std::uint16_t>(slot), "sample_root", static_cast<double>(newRoot));
    std::cout << "Transpose set to " << (semitones >= 0 ? "+" : "") << semitones
              << " semitones (root=" << newRoot << ") for slot " << slot << "\n";
    return;
  }

  // ── loop ──────────────────────────────────────────────────────────────────
  if (subcommand == "loop") {
    std::string modeStr;
    if (!(input >> modeStr)) {
      std::cout << "Usage: sample edit loop <slot> on|off|bidi|sustain [--start <val>] [--end <val>] [--unit s|f]\n";
      return;
    }

    int loopMode = -1;
    if (modeStr == "off")     loopMode = 0;
    else if (modeStr == "on") loopMode = 1;
    else if (modeStr == "bidi") loopMode = 2;
    else if (modeStr == "sustain") loopMode = 3;
    else {
      std::cout << "Unknown loop mode '" << modeStr << "' (use on, off, bidi, sustain)\n";
      return;
    }

    if (plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(slot)) == 0) {
      std::cout << "Sample slot " << slot << " is empty\n";
      return;
    }

    auto frameCount = plugins.sampleFrameCountForSlot(static_cast<std::uint16_t>(slot));
    auto sampleRate = plugins.sampleRateForSlot(static_cast<std::uint16_t>(slot));
    if (sampleRate == 0) sampleRate = 44100;

    std::string unit = "s";
    double startVal = -1.0, endVal = -1.0;
    bool hasStart = false, hasEnd = false;
    std::string token;

    while (input >> token) {
      if (token == "--start" && input >> token) {
        try { startVal = std::stod(token); hasStart = true; }
        catch (...) { std::cout << "Invalid --start value\n"; return; }
      } else if (token == "--end" && input >> token) {
        try { endVal = std::stod(token); hasEnd = true; }
        catch (...) { std::cout << "Invalid --end value\n"; return; }
      } else if (token == "--unit" && input >> token) {
        if (token == "s" || token == "f") { unit = token; }
        else { std::cout << "Invalid --unit (use 's' or 'f')\n"; return; }
      } else {
        std::cout << "Unknown option: " << token << "\n"; return;
      }
    }

    plugins.setSampleSlotParameter(static_cast<std::uint16_t>(slot), "loop_mode", static_cast<double>(loopMode));

    if (hasStart) {
      std::size_t startFrame = (unit == "s") ?
        SampleEditorUtils::secondsToFrames(startVal, sampleRate) : static_cast<std::size_t>(startVal);
      startFrame = std::min(startFrame, frameCount > 0 ? frameCount - 1 : 0);
      plugins.setSampleSlotParameter(static_cast<std::uint16_t>(slot), "loop_start", static_cast<double>(startFrame));
    }

    if (hasEnd) {
      std::size_t endFrame = (unit == "s") ?
        SampleEditorUtils::secondsToFrames(endVal, sampleRate) : static_cast<std::size_t>(endVal);
      endFrame = std::min(endFrame, frameCount);
      plugins.setSampleSlotParameter(static_cast<std::uint16_t>(slot), "loop_end", static_cast<double>(endFrame));
    }

    const char* loopNames[] = { "off", "forward (on)", "bidi", "sustain" };
    std::cout << "Loop set to " << loopNames[loopMode] << " for slot " << slot;
    if (loopMode != 0 && (hasStart || hasEnd)) {
      const double loopStartStored = plugins.getSampleSlotParameter(static_cast<std::uint16_t>(slot), "loop_start");
      const double loopEndStored   = plugins.getSampleSlotParameter(static_cast<std::uint16_t>(slot), "loop_end");
      std::cout << " (start=" << static_cast<std::size_t>(loopStartStored);
      if (loopEndStored > 0.0) {
        std::cout << " end=" << static_cast<std::size_t>(loopEndStored);
      } else {
        std::cout << " end=<sample end>";
      }
      std::cout << ")";
    }
    std::cout << "\n";
    return;
  }

  std::cout << "Unknown sample edit subcommand: " << subcommand << "\n";
}

}  // namespace extracker
