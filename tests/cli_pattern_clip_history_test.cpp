#include <cstdio>
#include <iostream>
#include <string>

namespace {

std::string run(const std::string& script) {
  const std::string cmd = "printf '" + script + "' | ./extracker 2>/dev/null";
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

}  // namespace

int main() {
  {
    const auto out = run(
        "pattern clip\\n"
        "pattern template blank\\n"
        "note set 0 0 60 1 100\\n"
        "note set 1 1 62 1 100\\n"
        "pattern copy 0 0 0 0\\n"
        "pattern copy 1 1 1 1\\n"
        "pattern clip list\\n"
        "pattern clip use 2\\n"
        "pattern paste 8\\n"
        "pattern print 8 8\\n"
        "pattern clip use 7\\n"
        "pattern clip use x\\n"
        "pattern clip clear\\n"
        "pattern paste 9\\n"
        "quit\\n");
    expect(out, "Clipboard history is empty. Use pattern copy first.", "empty history");
    expect(out, "* 1: 1x1  rows 1-1 ch 1  1 note D-5", "newest copy is slot 1 and active");
    expect(out, "  2: 1x1  rows 0-0 ch 0  1 note C-5", "older copy is slot 2");
    expect(out, "Clipboard slot 2 selected: 1x1  rows 0-0 ch 0", "select slot 2");
    expect(out, "Row 8: [60:i1:v100:f0:0]", "paste uses the selected slot at its source channel");
    expect(out, "No clipboard slot 7 (history has 2)", "out-of-range slot rejected");
    expect(out, "Usage: pattern clip", "bad slot token rejected");
    expect(out, "Clipboard history cleared", "clear");
    expect(out, "Clipboard is empty. Use pattern copy first.", "paste after clear");
  }

  if (failures != 0) {
    std::cerr << failures << " check(s) failed" << '\n';
    return 1;
  }
  std::cout << "cli pattern clip history test passed" << '\n';
  return 0;
}
