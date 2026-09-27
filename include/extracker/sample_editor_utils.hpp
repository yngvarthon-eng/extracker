#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace extracker {

struct SampleMetadata {
  std::string name;
  std::string path;
  std::uint8_t pan = 0x80;               // 0x00 (left) to 0xFF (right)
  float volume = 1.0f;                   // [0.1, 2.0] multiplier
  std::uint8_t rootMidiNote = 60;        // C4, for pitch reference during playback

  // Loop settings
  bool loopEnabled = false;
  enum class LoopMode : std::uint8_t {
    NONE = 0,
    FORWARD = 1,
    BIDIRECTIONAL = 2,
    SUSTAIN = 3
  };
  LoopMode loopMode = LoopMode::NONE;
  std::size_t loopStartFrame = 0;
  std::size_t loopEndFrame = 0;
};

class SampleEditorUtils {
public:
  // Frame-to-second conversion
  static std::size_t secondsToFrames(double seconds, std::uint32_t sampleRate);
  static double framesToSeconds(std::size_t frames, std::uint32_t sampleRate);

  // Validation
  static bool isValidFrameRange(std::size_t startFrame, std::size_t endFrame, std::size_t totalFrames);
  static bool isValidPanValue(const std::string& panStr, std::uint8_t& outPan);
  static bool isValidVolumeValue(const std::string& volStr, float& outVol);
};

}  // namespace extracker
