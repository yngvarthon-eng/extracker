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

  const std::string savePath = "/tmp/extracker_cli_pattern_copy_paste_fx_only_test.xtp";
  std::filesystem::remove(savePath);

  const std::string command =
      "printf 'pattern template blank\\n"
      "note fx 2 0 23 45\\n"
      "pattern copy 2 2 0 0\\n"
      "pattern paste 8 0\\n"
      "save " + savePath + "\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern FX-only copy/paste command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern FX-only copy/paste command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  int srcHasNote = -1;
  int srcFxCommand = -1;
  int srcFxValue = -1;
  int dstHasNote = -1;
  int dstFxCommand = -1;
  int dstFxValue = -1;

  const bool foundSource = extractStepFx(savePath, 2, 0, srcHasNote, srcFxCommand, srcFxValue);
  const bool foundDest = extractStepFx(savePath, 8, 0, dstHasNote, dstFxCommand, dstFxValue);

  if (!foundSource || !foundDest) {
    std::cerr << "Failed to find expected source/destination rows in saved file" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sourceMatches = srcHasNote == 0 && srcFxCommand == 23 && srcFxValue == 45;
  const bool destinationMatches = dstHasNote == 0 && dstFxCommand == 23 && dstFxValue == 45;

  if (!sourceMatches || !destinationMatches) {
    std::cerr << "FX-only copy/paste mismatch after save/load serialization check" << '\n';
    std::cerr << "Source row 2 ch 0: hasNote=" << srcHasNote << " fx=" << srcFxCommand << ":" << srcFxValue << '\n';
    std::cerr << "Dest row 8 ch 0: hasNote=" << dstHasNote << " fx=" << dstFxCommand << ":" << dstFxValue << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
