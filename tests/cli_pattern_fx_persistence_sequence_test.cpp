#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

bool extractStepFx(const std::string& path,
                   int wantedRow,
                   int wantedChannel,
                   int& hasNote,
                   int& effectCommand,
                   int& effectValue) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return false;
  }

  std::string line;
  while (std::getline(in, line)) {
    std::istringstream rowParser(line);
    int row = -1;
    int channel = -1;
    if (!(rowParser >> row >> channel)) {
      continue;
    }

    if (row != wantedRow || channel != wantedChannel) {
      continue;
    }

    int note = -1;
    int instrument = 0;
    int sample = 0;
    int gateTicks = 0;
    int velocity = 0;
    int retrigger = 0;
    if (!(rowParser >> hasNote >> note >> instrument >> sample >> gateTicks >> velocity >> retrigger >> effectCommand >> effectValue)) {
      return false;
    }

    return true;
  }

  return false;
}

}  // namespace

int main() {
  const std::string appPath = "./extracker";
  if (!std::filesystem::exists(appPath)) {
    std::cerr << "Expected CLI executable not found at " << appPath << '\n';
    return 1;
  }

  const std::string savePath = "/tmp/extracker_cli_pattern_fx_persistence_sequence_test.xtp";
  std::filesystem::remove(savePath);

  const std::string command =
      "printf 'pattern template blank\\n"
      "note fx 0 0 23 6\\n"
      "note fx 1 0 23 4\\n"
      "note fx 2 0 23 6\\n"
      "note fx 3 0 23 4\\n"
      "save " + savePath + "\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI FX persistence sequence command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI FX persistence sequence command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const int expectedFxValues[4] = {6, 4, 6, 4};
  for (int row = 0; row < 4; ++row) {
    int hasNote = -1;
    int fxCommand = -1;
    int fxValue = -1;
    if (!extractStepFx(savePath, row, 0, hasNote, fxCommand, fxValue)) {
      std::cerr << "Failed to parse row " << row << " channel 0 from saved file" << '\n';
      return 1;
    }

    if (hasNote != 0 || fxCommand != 23 || fxValue != expectedFxValues[row]) {
      std::cerr << "FX sequence mismatch at row " << row
                << ": hasNote=" << hasNote
                << " fx=" << fxCommand << ":" << fxValue
                << " expected fx=23:" << expectedFxValues[row] << '\n';
      std::cerr << output << '\n';
      return 1;
    }
  }

  return 0;
}
