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
      "printf 'record on 0\\n"
      "record off\\n"
      "record quantize on\\n"
      "record quantize status\\n"
      "record quantize off\\n"
      "record quantize status\\n"
      "record quantize nope\\n"
      "record overdub on\\n"
      "record overdub status\\n"
      "record overdub off\\n"
      "record overdub status\\n"
      "record overdub nope\\n"
      "quit\\n' | " + appPath;

  std::array<char, 512> buffer{};
  std::string output;

  FILE* pipe = popen(command.c_str(), "r");
  if (pipe == nullptr) {
    std::cerr << "Failed to spawn CLI record mode toggle command" << '\n';
    return 1;
  }

  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }

  const int status = pclose(pipe);
  if (status != 0) {
    std::cerr << "CLI record mode toggle command exited non-zero: " << status << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  const bool sawRecordDisabled   = output.find("Record disabled") != std::string::npos;
  const bool sawQuantizeEnabled  = output.find("Record quantize enabled") != std::string::npos;
  const bool sawQuantizeStatusOn = output.find("Record quantize: on") != std::string::npos;
  const bool sawQuantizeDisabled = output.find("Record quantize disabled") != std::string::npos;
  const bool sawQuantizeStatusOff = output.find("Record quantize: off") != std::string::npos;
  const bool sawQuantizeUsage    = output.find("Usage: record quantize <on|off|status>") != std::string::npos;
  const bool sawOverdubEnabled   = output.find("Record overdub enabled") != std::string::npos;
  const bool sawOverdubStatusOn  = output.find("Record overdub: on") != std::string::npos;
  const bool sawOverdubDisabled  = output.find("Record overdub disabled") != std::string::npos;
  const bool sawOverdubStatusOff = output.find("Record overdub: off") != std::string::npos;
  const bool sawOverdubUsage     = output.find("Usage: record overdub <on|off|status>") != std::string::npos;

  if (!sawRecordDisabled || !sawQuantizeEnabled || !sawQuantizeStatusOn ||
      !sawQuantizeDisabled || !sawQuantizeStatusOff || !sawQuantizeUsage ||
      !sawOverdubEnabled || !sawOverdubStatusOn ||
      !sawOverdubDisabled || !sawOverdubStatusOff || !sawOverdubUsage) {
    std::cerr << "Missing expected record mode toggle output markers" << '\n';
    std::cerr << output << '\n';
    return 1;
  }

  return 0;
}
