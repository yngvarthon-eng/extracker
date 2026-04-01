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
      "printf 'record quantize off\\n"
      "record on 0\\n"
      "record note 70 instr\\n"
      "record note 71 vel\\n"
      "record note 72 fx 4\\n"
      "record note 73 instr 1 instr 2\\n"
      "record note 74 vel 80 banana 1\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record note invalid command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record note invalid command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const std::string usageNeedle =
      "Usage: record note [dry] <midi> [instr] [vel] [fx] [fxval] | record note [dry] <midi> vel <vel> [fx] [fxval] | record note [dry] <midi> fx <fx> <fxval> | record note [dry] <midi> instr <i> [vel <v>] [fx <f> <fv>]";

  std::size_t count = 0;
  std::size_t pos = output.find(usageNeedle);
  while (pos != std::string::npos) {
    ++count;
    pos = output.find(usageNeedle, pos + usageNeedle.size());
  }

  if (count < 5) {
    std::cerr << "Expected usage output for each invalid record note form" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
