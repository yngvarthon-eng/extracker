#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

bool runCommandCapture(const std::string& command, std::string& output) {
  std::array<char, 1024> buffer{};
  output.clear();

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return false;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  return status == 0;
}

std::string readFileText(const std::filesystem::path& path) {
  std::ifstream in(path);
  if (!in.is_open()) {
    return {};
  }
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

bool writeLegacyV2Fixture(const std::filesystem::path& path) {
  constexpr int kRows = 64;
  constexpr int kChannels = 8;

  std::ofstream out(path);
  if (!out.is_open()) {
    return false;
  }

  out << "EXTRACKER_PATTERN_V2 " << kRows << " " << kChannels << "\n";
  for (int row = 0; row < kRows; ++row) {
    for (int channel = 0; channel < kChannels; ++channel) {
      out << row << " " << channel << " "
          << 0 << " "      // hasNote
          << -1 << " "     // note
          << 0 << " "      // instrument
          << 65535 << " "  // sample sentinel
          << 0 << " "      // gate
          << 100 << " "    // velocity
          << 0 << " "      // retrigger
          << 0 << " "      // effect command
          << 0 << "\n";    // effect value
    }
  }

  out << "PATTERN_SWING 81\n";
  return out.good();
}

bool containsLine(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  const std::string appPath = "./extracker";
  if (!std::filesystem::exists(appPath)) {
    std::cerr << "Expected CLI executable not found at " << appPath << '\n';
    return 1;
  }

  namespace fs = std::filesystem;
  const fs::path basePath = fs::temp_directory_path() / "extracker_cli_legacy_v2_pattern_swing_load_test";
  const fs::path legacyPath = basePath.string() + "_legacy_v2.xtp";
  const fs::path savedPath = basePath.string() + "_saved.xtp";

  std::error_code ec;
  fs::remove(legacyPath, ec);
  fs::remove(savedPath, ec);

  if (!writeLegacyV2Fixture(legacyPath)) {
    std::cerr << "Failed to write legacy V2 fixture file" << '\n';
    return 1;
  }

  const std::string command =
      "printf 'load " + legacyPath.string() + "\\n"
      "save " + savedPath.string() + "\\n"
      "quit\\n' | " + appPath;

  std::string output;
  if (!runCommandCapture(command, output)) {
    std::cerr << "Failed to run CLI legacy V2 load/save command" << '\n';
    return 1;
  }

  if (output.find("Module loaded from " + legacyPath.string()) == std::string::npos ||
      output.find("Module saved to " + savedPath.string()) == std::string::npos) {
    std::cerr << "Missing expected load/save success output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const std::string savedText = readFileText(savedPath);
  if (savedText.empty()) {
    std::cerr << "Failed to read saved roundtrip module file" << '\n';
    return 1;
  }

  if (!containsLine(savedText, "EXTRACKER_SONG_V1")) {
    std::cerr << "Round-tripped file is missing EXTRACKER_SONG_V1 header" << '\n';
    std::cerr << savedText << '\n';
    return 1;
  }

  if (!containsLine(savedText, "PATTERN_SWING 75")) {
    std::cerr << "Round-tripped file missing expected clamped PATTERN_SWING" << '\n';
    std::cerr << savedText << '\n';
    return 1;
  }

  fs::remove(legacyPath, ec);
  fs::remove(savedPath, ec);
  return 0;
}
