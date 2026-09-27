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
  const fs::path basePath = fs::temp_directory_path() / "extracker_cli_pattern_swing_malformed_tail_test";
  const fs::path baselinePath = basePath.string() + "_baseline.xtp";
  const fs::path malformedPath = basePath.string() + "_malformed.xtp";
  const fs::path finalPath = basePath.string() + "_final.xtp";

  std::error_code ec;
  fs::remove(baselinePath, ec);
  fs::remove(malformedPath, ec);
  fs::remove(finalPath, ec);

  const std::string setupCommand =
      "printf 'pattern insert after\\n"
      "pattern insert after\\n"
      "save " + baselinePath.string() + "\\n"
      "quit\\n' | " + appPath;

  std::string output;
  if (!runCommandCapture(setupCommand, output)) {
    std::cerr << "Failed to run setup command" << '\n';
    return 1;
  }

  std::string baselineText = readFileText(baselinePath);
  if (baselineText.empty()) {
    std::cerr << "Failed to read baseline module file" << '\n';
    return 1;
  }

  if (!replacePatternSwingLine(baselineText, "PATTERN_SWING 72 73")) {
    std::cerr << "Baseline file missing PATTERN_SWING line" << '\n';
    return 1;
  }

  if (!writeFileText(malformedPath, baselineText)) {
    std::cerr << "Failed to write malformed module file" << '\n';
    return 1;
  }

  const std::string verifyCommand =
      "printf 'load " + malformedPath.string() + "\\n"
      "save " + finalPath.string() + "\\n"
      "quit\\n' | " + appPath;

  if (!runCommandCapture(verifyCommand, output)) {
    std::cerr << "Failed to run verification command" << '\n';
    return 1;
  }

  if (output.find("Failed to load module from " + malformedPath.string()) == std::string::npos) {
    std::cerr << "Malformed PATTERN_SWING load was not rejected" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  if (output.find("Module saved to " + finalPath.string()) == std::string::npos) {
    std::cerr << "Expected save after failed load did not occur" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const std::string finalText = readFileText(finalPath);
  if (finalText.empty()) {
    std::cerr << "Failed to read final module file" << '\n';
    return 1;
  }

  if (!containsLine(finalText, "PATTERN_SWING 50 50 50")) {
    std::cerr << "State changed after malformed swing load (expected unchanged defaults)" << '\n';
    std::cerr << finalText << '\n';
    return 1;
  }

  fs::remove(baselinePath, ec);
  fs::remove(malformedPath, ec);
  fs::remove(finalPath, ec);
  return 0;
}
