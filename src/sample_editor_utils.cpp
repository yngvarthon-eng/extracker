#include "extracker/sample_editor_utils.hpp"
#include <cctype>
#include <cstdlib>

namespace extracker {

std::size_t SampleEditorUtils::secondsToFrames(double seconds, std::uint32_t sampleRate) {
  if (seconds < 0.0 || sampleRate == 0) {
    return 0;
  }
  return static_cast<std::size_t>(seconds * sampleRate + 0.5);
}

double SampleEditorUtils::framesToSeconds(std::size_t frames, std::uint32_t sampleRate) {
  if (sampleRate == 0) {
    return 0.0;
  }
  return static_cast<double>(frames) / sampleRate;
}

bool SampleEditorUtils::isValidFrameRange(std::size_t startFrame, std::size_t endFrame, std::size_t totalFrames) {
  return startFrame < endFrame && endFrame <= totalFrames;
}

bool SampleEditorUtils::isValidPanValue(const std::string& panStr, std::uint8_t& outPan) {
  if (panStr == "L" || panStr == "l") {
    outPan = 0x00;
    return true;
  }
  if (panStr == "C" || panStr == "c") {
    outPan = 0x80;
    return true;
  }
  if (panStr == "R" || panStr == "r") {
    outPan = 0xFF;
    return true;
  }

  // Try hex parse (0x00-0xFF)
  if (panStr.length() >= 2 && panStr[0] == '0' && (panStr[1] == 'x' || panStr[1] == 'X')) {
    char* endptr = nullptr;
    long val = std::strtol(panStr.c_str(), &endptr, 16);
    if (endptr != panStr.c_str() + panStr.length() || val < 0 || val > 255) {
      return false;
    }
    outPan = static_cast<std::uint8_t>(val);
    return true;
  }

  // Try decimal parse (0-255)
  char* endptr = nullptr;
  long val = std::strtol(panStr.c_str(), &endptr, 10);
  if (endptr != panStr.c_str() + panStr.length() || val < 0 || val > 255) {
    return false;
  }
  outPan = static_cast<std::uint8_t>(val);
  return true;
}

bool SampleEditorUtils::isValidVolumeValue(const std::string& volStr, float& outVol) {
  char* endptr = nullptr;
  double val = std::strtod(volStr.c_str(), &endptr);
  if (endptr == volStr.c_str() || endptr != volStr.c_str() + volStr.length()) {
    return false;
  }
  if (val < 0.1 || val > 2.0) {
    return false;
  }
  outVol = static_cast<float>(val);
  return true;
}

}  // namespace extracker
