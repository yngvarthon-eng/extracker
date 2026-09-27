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
  const std::string setup =
      "pattern template blank\\n"
      "note set 0 0 60 1 100\\n"
      "note set 0 1 62 1 100\\n"
      "ch name 1 Bass\\n";

  {
    const auto out = run(setup + "ch insert 0\\npattern print 0 0\\nch name 2\\n"
                                 "ch move 2 0\\npattern print 0 0\\n"
                                 "ch dup 0\\npattern print 0 0\\nch delete 1\\npattern print 0 0\\nquit\\n");
    expect(out, "Inserted empty channel at 0", "insert message");
    expect(out, "Row 0: [--] [60:i1:v100:f0:0] [62:i1:v100:f0:0] [--]", "insert shifts right");
    expect(out, "Channel 2 name: Bass", "insert carries name");
    expect(out, "Moved channel 2 to 0", "move message");
    expect(out, "Row 0: [62:i1:v100:f0:0] [--] [60:i1:v100:f0:0] [--]", "move reorders");
    expect(out, "Duplicated channel 0 into 1", "dup message");
    expect(out, "Row 0: [62:i1:v100:f0:0] [62:i1:v100:f0:0] [--] [60:i1:v100:f0:0]", "dup copies");
    expect(out, "Deleted channel 1", "delete message");
    expect(out, "Row 0: [62:i1:v100:f0:0] [--] [60:i1:v100:f0:0] [--]", "delete shifts left");
  }

  {
    const auto out = run(setup + "ch insert 0\\nch move 2 0\\nch undo\\nch undo\\npattern print 0 0\\n"
                                 "ch redo\\npattern print 0 0\\nnote set 5 5 50 1 100\\nch undo\\nch undo\\nquit\\n");
    expect(out, "Row 0: [60:i1:v100:f0:0] [62:i1:v100:f0:0] [--]", "two undos restore original");
    expect(out, "Channel redo applied", "redo");
    expect(out, "Row 0: [--] [60:i1:v100:f0:0] [62:i1:v100:f0:0]", "redo reapplies insert");
    expect(out, "Channel undo failed: patterns changed since the channel edit", "undo refused after note edit");
    expect(out, "Channel undo failed: nothing to undo", "stale history cleared");
  }

  {
    const auto out = run(setup + "note set 0 15 40 1 100\\nch insert 3\\nch dup 15\\nch move 0 99\\nquit\\n");
    expect(out, "Channel insert failed: channel 15 is not empty in pattern 0", "insert blocked");
    expect(out, "Channel dup failed: no room after the last channel", "dup of last channel blocked");
    expect(out, "Invalid channel index: 99", "bad channel rejected");
  }

  {
    // Pattern undo cannot restore a pre-move layout after a channel edit.
    const auto out = run(setup + "pattern transpose 1\\nch move 1 0\\npattern undo\\nquit\\n");
    expect(out, "No bulk undo state available", "pattern undo discarded after channel edit");
  }

  if (failures != 0) {
    std::cerr << failures << " check(s) failed" << '\n';
    return 1;
  }
  std::cout << "cli channel layout test passed" << '\n';
  return 0;
}
