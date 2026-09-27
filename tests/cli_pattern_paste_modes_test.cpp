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
  // Rows 0-1 of channel 0 hold a note and an empty step; row 5 is already occupied.
  const std::string setup =
      "pattern template blank\\n"
      "note set 0 0 60 1 100 2 3\\n"
      "note set 5 0 70 1 100\\n"
      "pattern copy 0 1 0 0\\n";

  {
    const auto out = run(setup + "pattern paste 4 mode mix\\npattern print 4 5\\nquit\\n");
    expect(out, "Pasted 1 step(s) at row 4 (channel offset 0, 0 skipped) [mix]", "mix counts only filled cells");
    expect(out, "Row 4: [60:i1:v100:f2:3]", "mix fills empty row");
    expect(out, "Row 5: [70:i1:v100:f0:0]", "mix keeps occupied row");
  }

  {
    const auto out = run(setup + "pattern paste 4 mode overwrite\\npattern print 5 5\\nquit\\n");
    expect(out, "[overwrite]", "overwrite mode tag");
    expect(out, "Row 5: [--]", "overwrite clears with empty source step");
  }

  {
    const auto out = run(setup + "pattern paste 4 mode insert\\npattern print 4 7\\nquit\\n");
    expect(out, "[insert]", "insert mode tag");
    expect(out, "Row 4: [60:i1:v100:f2:3]", "insert pastes block");
    expect(out, "Row 7: [70:i1:v100:f0:0]", "insert pushes existing row down by block height");
  }

  {
    // pattern print hides effect-only cells, so check the plan through the dry-run preview.
    const auto out = run(setup + "pattern paste dry preview verbose 0 1 flood cols f\\n"
                                 "pattern paste 0 1 flood cols f\\nquit\\n");
    expect(out, "Pasted 32 step(s) at row 0 (channel offset 1, 0 skipped) [flood] [dry-run]", "flood dry run");
    expect(out, "src 0:0 -> dst 2:1 note -1 i0 v100 fx2:3", "effect-only flood repeats every block height");
    expect(out, "Pasted 32 step(s) at row 0 (channel offset 1, 0 skipped) [flood]\n", "flood applied");
  }

  {
    const auto out = run(setup + "pattern paste 0 cols x\\npattern paste 0 mode sideways\\n"
                                 "pattern paste 0 mode insert flood\\nquit\\n");
    expect(out, "Usage: pattern paste", "bad columns or mode rejected");
    expect(out, "flood cannot be combined with mode insert", "insert + flood rejected");
  }

  if (failures != 0) {
    std::cerr << failures << " check(s) failed" << '\n';
    return 1;
  }
  std::cout << "cli pattern paste modes test passed" << '\n';
  return 0;
}
