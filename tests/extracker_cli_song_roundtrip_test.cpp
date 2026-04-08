#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

bool runCommandCapture(const std::string& command, std::string& output) {
  std::array<char, 256> buffer{};
  output.clear();

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    return false;
  }

  while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int rc = pclose(pipe);
  return rc == 0;
}

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

std::size_t countOccurrences(const std::string& text, const std::string& needle) {
  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = text.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

}  // namespace

int main() {
  namespace fs = std::filesystem;

  fs::path tempFile = fs::temp_directory_path() / "extracker_song_roundtrip_test";
  std::error_code ec;
  fs::remove(tempFile, ec);

  const std::string setupCommands =
      "printf \"pattern insert after\\npattern insert after\\nsave " + tempFile.string() + "\\nquit\\n\" | ./extracker";

  std::string output;
  if (!runCommandCapture(setupCommands, output)) {
    std::cerr << "Failed to run CLI setup/save command" << '\n';
    return 1;
  }

  fs::path savedFile = tempFile;
  if (!fs::exists(savedFile)) {
    savedFile += ".xtp";
  }

  std::ifstream in(savedFile);
  if (!in.is_open()) {
    std::cerr << "Failed to open saved song file" << '\n';
    return 1;
  }

  std::ostringstream ss;
  ss << in.rdbuf();
  const std::string fileText = ss.str();

  if (!contains(fileText, "EXTRACKER_SONG_V1")) {
    std::cerr << "Saved file missing EXTRACKER_SONG_V1 header" << '\n';
    return 1;
  }
  if (countOccurrences(fileText, "PATTERN ") != 3) {
    std::cerr << "Saved file missing expected number of PATTERN blocks" << '\n';
    return 1;
  }
  if (!contains(fileText, "SONG_ORDER 0 1 2")) {
    std::cerr << "Saved file missing expected song order" << '\n';
    return 1;
  }

    const std::string loadCommands =
        "printf \"load " + savedFile.string() + "\\nstatus detail\\nquit\\n\" | ./extracker";

  if (!runCommandCapture(loadCommands, output)) {
    std::cerr << "Failed to run CLI load/status command" << '\n';
    return 1;
  }

  if (!contains(output, "Module loaded from ")) {
    std::cerr << "Loaded module did not report successful load" << '\n';
    return 1;
  }

  fs::remove(tempFile, ec);
  fs::remove(savedFile, ec);
  return 0;
}
