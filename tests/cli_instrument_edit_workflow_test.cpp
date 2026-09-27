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
  constexpr std::uint32_t frameCount = 64;
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
    const std::int16_t sample = (i % 16 < 8) ? 10000 : -10000;
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

  const std::filesystem::path wavPath =
      std::filesystem::current_path() / "cli_instrument_edit_workflow_test.wav";
  std::filesystem::remove(wavPath);
  if (!writeTestWav(wavPath)) {
    std::cerr << "Failed to write test WAV file" << '\n';
    return 1;
  }

  const std::string command =
      "printf 'sample load 4 clap " + wavPath.filename().string() + "\n"
      "instrument sample 2 4\n"
      "instrument edit gain 2 1.5\n"
      "instrument edit pan 2 R\n"
      "instrument edit root 2 64\n"
      "instrument edit loop 2 on 2 20\n"
      "instrument edit get 2 gain\n"
      "instrument edit info 2\n"
      "instrument status 2\n"
      "quit\n' | " + appPath;

  std::array<char, 1024> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::filesystem::remove(wavPath);
    std::cerr << "Failed to spawn CLI instrument workflow command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  std::filesystem::remove(wavPath);
  if (status != 0) {
    std::cerr << "CLI instrument workflow command exited non-zero: " << status
              << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawSampleLoad =
      output.find("Sample slot 4 \"clap\": cli_instrument_edit_workflow_test.wav") !=
      std::string::npos;
  const bool sawAssignSample =
      output.find("Assigned sample slot 4 to instrument 2") != std::string::npos;
  const bool sawGainSet =
      output.find("Instrument 2 gain = 1.5") != std::string::npos;
  const bool sawPanSet =
      output.find("Instrument 2 pan = 1") != std::string::npos;
  const bool sawRootSet =
      output.find("Instrument 2 sample_root = 64") != std::string::npos;
  const bool sawLoopMode =
      output.find("Instrument 2 loop_mode = 1") != std::string::npos;
  const bool sawLoopStart =
      output.find("Instrument 2 loop_start = 2") != std::string::npos;
  const bool sawLoopEnd =
      output.find("Instrument 2 loop_end = 20") != std::string::npos;
  const bool sawInfo =
      output.find("Instrument 2: builtin.sample") != std::string::npos;
  const bool sawStatus =
      output.find("[2] builtin.sample (sample slot 4)") != std::string::npos;

  if (!sawSampleLoad || !sawAssignSample || !sawGainSet || !sawPanSet ||
      !sawRootSet || !sawLoopMode || !sawLoopStart || !sawLoopEnd || !sawInfo ||
      !sawStatus) {
    std::cerr << "Missing expected instrument workflow output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
