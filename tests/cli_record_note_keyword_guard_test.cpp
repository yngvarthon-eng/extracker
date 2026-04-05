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
      "record note 70 vel 90 vel 80\\n"
      "record note 71 fx 4 44 fx 5 55\\n"
      "record note 72 vel 88 fx 6\\n"
      "quit\\n' | " + appPath;

  std::array<char, 2048> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record note keyword guard command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record note keyword guard command exited non-zero: " << status << '\n';
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

  if (count < 3) {
    std::cerr << "Expected usage output for each keyword-guard invalid form" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
