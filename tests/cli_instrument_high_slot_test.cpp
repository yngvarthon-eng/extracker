// Instrument slots above 15 work end-to-end in the CLI: assignment, per-
// instrument effects and reverb sends (including the list/reset/clear commands
// that walk every slot), and save/load.
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::string run(const std::string& script) {
  // timeout guards the slot-walking commands against looping forever.
  const std::string cmd = "printf '" + script + "' | timeout 20 ./extracker 2>/dev/null";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) return {};
  std::string result;
  char buf[512];
  while (fgets(buf, sizeof(buf), pipe)) result += buf;
  pclose(pipe);
  return result;
}

int failures = 0;

void expect(const std::string& out, const std::string& needle, const char* what) {
  if (out.find(needle) == std::string::npos) {
    std::cerr << "FAIL: " << what << " (missing \"" << needle << "\")\n";
    ++failures;
  }
}

void expectNot(const std::string& out, const std::string& needle, const char* what) {
  if (out.find(needle) != std::string::npos) {
    std::cerr << "FAIL: " << what << " (unexpected \"" << needle << "\")\n";
    ++failures;
  }
}

}  // namespace

int main() {
  const auto fx = run(
      "plugin assign 200 builtin.square\\n"
      "effects delay 200 300 100 100\\n"
      "effects list\\n"
      "effects reset all\\n"
      "effects list\\n"
      "quit\\n");
  expect(fx, "Assigned builtin.square to instrument 200", "assign to instrument 200");
  expect(fx, "\n200 ", "effects list shows instrument 200");
  expect(fx, "All instruments: effects/filter/pitch reset", "effects reset all runs");
  expect(fx, "Final status", "effects commands return (no endless loop)");
  const auto afterReset = fx.substr(fx.find("All instruments: effects/filter/pitch reset"));
  expectNot(afterReset, "\n200 ", "effects reset all clears instrument 200");

  const auto rev = run(
      "reverb send 180 128\\n"
      "reverb get\\n"
      "reverb clear\\n"
      "reverb get\\n"
      "quit\\n");
  expect(rev, "Instr 180 send=", "reverb send on instrument 180");
  const auto afterClear = rev.substr(rev.find("Reverb cleared"));
  expect(afterClear, "(no instrument sends active)", "reverb clear resets instrument 180");

  const std::string path = "/tmp/xt_cli_instrument_high_slot_test.xtp";
  std::filesystem::remove(path);
  run("plugin assign 255 builtin.sine\\nsave " + path + "\\nquit\\n");
  const auto loaded = run("load " + path + "\\ninstrument list\\nquit\\n");
  expect(loaded, "255", "instrument 255 survives save/load");
  std::filesystem::remove(path);

  if (failures != 0) {
    std::cerr << failures << " failure(s)\n";
    return 1;
  }
  return 0;
}
