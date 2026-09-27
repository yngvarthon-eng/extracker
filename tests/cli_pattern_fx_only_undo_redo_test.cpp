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

  const std::string saveApplied = "/tmp/extracker_cli_pattern_fx_only_undo_redo_applied.xtp";
  const std::string saveUndone = "/tmp/extracker_cli_pattern_fx_only_undo_redo_undone.xtp";
  const std::string saveRedone = "/tmp/extracker_cli_pattern_fx_only_undo_redo_redone.xtp";
  std::filesystem::remove(saveApplied);
  std::filesystem::remove(saveUndone);
  std::filesystem::remove(saveRedone);

  const std::string command =
      "printf 'pattern template blank\\n"
      "note fx 2 0 23 45\\n"
      "pattern copy 2 2 0 0\\n"
      "pattern paste 8 0\\n"
      "save " + saveApplied + "\\n"
      "pattern undo\\n"
      "save " + saveUndone + "\\n"
      "pattern redo\\n"
      "save " + saveRedone + "\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI FX-only undo/redo command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI FX-only undo/redo command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawUndo = output.find("Bulk pattern undo applied") != std::string::npos;
  const bool sawRedo = output.find("Bulk pattern redo applied") != std::string::npos;
  if (!sawUndo || !sawRedo) {
    std::cerr << "Missing expected undo/redo output markers for FX-only edit" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  int hasNoteApplied = -1;
  int fxApplied = -1;
  int fvApplied = -1;
  int hasNoteUndone = -1;
  int fxUndone = -1;
  int fvUndone = -1;
  int hasNoteRedone = -1;
  int fxRedone = -1;
  int fvRedone = -1;

  const bool okApplied = extractStepFx(saveApplied, 8, 0, hasNoteApplied, fxApplied, fvApplied);
  const bool okUndone = extractStepFx(saveUndone, 8, 0, hasNoteUndone, fxUndone, fvUndone);
  const bool okRedone = extractStepFx(saveRedone, 8, 0, hasNoteRedone, fxRedone, fvRedone);
  if (!okApplied || !okUndone || !okRedone) {
    std::cerr << "Failed to parse saved files for FX-only undo/redo assertions" << '\n';
    return 1;
  }

  const bool appliedOk = hasNoteApplied == 0 && fxApplied == 23 && fvApplied == 45;
  const bool undoneOk = hasNoteUndone == 0 && fxUndone == 0 && fvUndone == 0;
  const bool redoneOk = hasNoteRedone == 0 && fxRedone == 23 && fvRedone == 45;

  if (!appliedOk || !undoneOk || !redoneOk) {
    std::cerr << "FX-only undo/redo state mismatch" << '\n';
    std::cerr << "Applied: hasNote=" << hasNoteApplied << " fx=" << fxApplied << ':' << fvApplied << '\n';
    std::cerr << "Undone : hasNote=" << hasNoteUndone << " fx=" << fxUndone << ':' << fvUndone << '\n';
    std::cerr << "Redone : hasNote=" << hasNoteRedone << " fx=" << fxRedone << ':' << fvRedone << '\n';
    return 1;
  }

  return 0;
}
