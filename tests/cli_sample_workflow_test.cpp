#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void writeWord(std::ofstream& out, std::uint32_t value, int bytes) {
  for (int i = 0; i < bytes; ++i) {
    out.put(static_cast<char>((value >> (8 * i)) & 0xFFu));
  }
}

bool writeTestWav(const std::filesystem::path& path) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }

  constexpr std::uint16_t channelCount = 1;
  constexpr std::uint32_t sampleRate = 44100;
  constexpr std::uint16_t bitsPerSample = 16;
  constexpr std::uint16_t bytesPerSample = bitsPerSample / 8;
  constexpr std::uint32_t frameCount = 32;
  constexpr std::uint32_t dataSize = frameCount * channelCount * bytesPerSample;
  constexpr std::uint32_t riffSize = 36 + dataSize;
  constexpr std::uint32_t byteRate = sampleRate * channelCount * bytesPerSample;
  constexpr std::uint16_t blockAlign = channelCount * bytesPerSample;

  out.write("RIFF", 4);
  writeWord(out, riffSize, 4);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  writeWord(out, 16, 4);
  writeWord(out, 1, 2);
  writeWord(out, channelCount, 2);
  writeWord(out, sampleRate, 4);
  writeWord(out, byteRate, 4);
  writeWord(out, blockAlign, 2);
  writeWord(out, bitsPerSample, 2);
  out.write("data", 4);
  writeWord(out, dataSize, 4);

  for (std::uint32_t i = 0; i < frameCount; ++i) {
    const std::int16_t sample = (i % 8 < 4) ? 12000 : -12000;
    writeWord(out, static_cast<std::uint16_t>(sample), 2);
  }

  return static_cast<bool>(out);
}

}  // namespace

int main() {
  const std::string appPath = "./extracker";
  if (!std::filesystem::exists(appPath)) {
    std::cerr << "Expected CLI executable not found at " << appPath << '\n';
    return 1;
  }

  const std::filesystem::path wavPath = std::filesystem::current_path() / "cli_sample_workflow_test.wav";
  std::filesystem::remove(wavPath);
  if (!writeTestWav(wavPath)) {
    std::cerr << "Failed to write test WAV file" << '\n';
    return 1;
  }

  const std::string command =
      "printf 'sample load 3 kick " + wavPath.filename().string() + "\n"
      "sample rename 3 snare\n"
      "sample status 3\n"
      "sample play 3 64\n"
      "sample stop 3 64\n"
      "sample unload 3\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::filesystem::remove(wavPath);
    std::cerr << "Failed to spawn CLI sample workflow command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  std::filesystem::remove(wavPath);
  if (status != 0) {
    std::cerr << "CLI sample workflow command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawLoad = output.find("Sample slot 3 \"kick\": cli_sample_workflow_test.wav") != std::string::npos;
  const bool sawRename = output.find("Renamed sample slot 3 to \"snare\"") != std::string::npos;
  const bool sawStatus = output.find("Sample slot 3: \"snare\" -> cli_sample_workflow_test.wav") != std::string::npos;
  const bool sawPlay = output.find("Previewing sample slot 3 at MIDI note 64") != std::string::npos;
  const bool sawStop = output.find("Stopped sample slot 3 at MIDI note 64") != std::string::npos;
  const bool sawUnload = output.find("Unloaded sample slot 3 (\"snare\")") != std::string::npos;

  if (!sawLoad || !sawRename || !sawStatus || !sawPlay || !sawStop || !sawUnload) {
    std::cerr << "Missing expected sample workflow output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
