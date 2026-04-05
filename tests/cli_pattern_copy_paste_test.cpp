#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

int main() {
  const std::string appPath = "./extracker";
  if (!std::filesystem::exists(appPath)) {
    std::cerr << "Expected CLI executable not found at " << appPath << '\n';
    return 1;
  }

  const std::string command =
      "printf 'pattern template blank\\n"
      "note set 0 0 60 1 100 2 3\\n"
      "note set 1 1 62 2 110 4 5\\n"
      "pattern copy 0 1 0 1\\n"
      "pattern paste dry preview verbose 4 2\\n"
      "pattern paste 4 2\\n"
      "pattern print 4 5\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI pattern copy/paste command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI pattern copy/paste command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawCopy = output.find("Copied rows 0..1 channels 0..1 (2x2)") != std::string::npos;
  const bool sawDryPaste = output.find("Pasted 2 step(s) at row 4 (channel offset 2, 0 skipped) [dry-run]") != std::string::npos;
  const bool sawPreview = output.find("Paste preview:") != std::string::npos;
  const bool sawPreviewLine = output.find("src 0:0 -> dst 4:2 note 60 i1 v100 fx2:3") != std::string::npos;
  const bool sawCommitPaste = output.find("Pasted 2 step(s) at row 4 (channel offset 2, 0 skipped)") != std::string::npos;
  const bool sawRow4 = output.find("Row 4: [--] [--] [60:i1:v100:f2:3]") != std::string::npos;
  const bool sawRow5 = output.find("Row 5: [--] [--] [--] [62:i2:v110:f4:5]") != std::string::npos;

  if (!sawCopy || !sawDryPaste || !sawPreview || !sawPreviewLine || !sawCommitPaste ||
      !sawRow4 || !sawRow5) {
    std::cerr << "Missing expected pattern copy/paste output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
