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

bool writeFileText(const std::filesystem::path& path, const std::string& text) {
  std::ofstream out(path);
  if (!out.is_open()) {
    return false;
  }
  out << text;
  return out.good();
}

bool replacePatternSwingLine(std::string& text, const std::string& replacementLine) {
  const std::string marker = "PATTERN_SWING";
  const std::size_t start = text.find(marker);
  if (start == std::string::npos) {
    return false;
  }

  std::size_t end = text.find('\n', start);
  if (end == std::string::npos) {
    end = text.size();
  }

  text.replace(start, end - start, replacementLine);
  return true;
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
  const fs::path basePath = fs::temp_directory_path() / "extracker_cli_pattern_swing_save_load_test";
  const fs::path firstPath = basePath.string() + "_first.xtp";
  const fs::path secondPath = basePath.string() + "_second.xtp";

  std::error_code ec;
  fs::remove(firstPath, ec);
  fs::remove(secondPath, ec);

  const std::string setupCommand =
      "printf 'pattern insert after\\n"
      "pattern insert after\\n"
      "save " + firstPath.string() + "\\n"
      "quit\\n' | " + appPath;

  std::string output;
  if (!runCommandCapture(setupCommand, output)) {
    std::cerr << "Failed to run CLI setup command" << '\n';
    return 1;
  }

  std::string firstText = readFileText(firstPath);
  if (firstText.empty()) {
    std::cerr << "Failed to read first saved module file" << '\n';
    return 1;
  }

  if (!replacePatternSwingLine(firstText, "PATTERN_SWING 72 49 100")) {
    std::cerr << "Saved file missing PATTERN_SWING line" << '\n';
    return 1;
  }

  if (!writeFileText(firstPath, firstText)) {
    std::cerr << "Failed to write modified first module file" << '\n';
    return 1;
  }

  const std::string roundtripCommand =
      "printf 'load " + firstPath.string() + "\\n"
      "save " + secondPath.string() + "\\n"
      "quit\\n' | " + appPath;

  if (!runCommandCapture(roundtripCommand, output)) {
    std::cerr << "Failed to run CLI load/save roundtrip command" << '\n';
    return 1;
  }

  if (output.find("Module loaded from " + firstPath.string()) == std::string::npos ||
      output.find("Module saved to " + secondPath.string()) == std::string::npos) {
    std::cerr << "Missing expected load/save success output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const std::string secondText = readFileText(secondPath);
  if (secondText.empty()) {
    std::cerr << "Failed to read second saved module file" << '\n';
    return 1;
  }

  if (!containsLine(secondText, "PATTERN_SWING 72 50 75")) {
    std::cerr << "Round-tripped file missing expected clamped PATTERN_SWING values" << '\n';
    std::cerr << secondText << '\n';
    return 1;
  }

  fs::remove(firstPath, ec);
  fs::remove(secondPath, ec);
  return 0;
}
