#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

std::string run(const std::string& script) {
  const std::string cmd = "echo '" + script + "' | ./extracker 2>/dev/null";
  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) return {};
  std::string result;
  char buf[256];
  while (fgets(buf, sizeof(buf), pipe)) result += buf;
  pclose(pipe);
  return result;
}

int failures = 0;

void expect(const std::string& out, const std::string& needle, const char* what) {
  if (out.find(needle) == std::string::npos) {
    std::cerr << "FAIL: " << what << " (missing \"" << needle << "\")\n" << out << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  // Validation.
  {
    const auto out = run("record chord 60 64; record on 5; record chord 60 64; ch track add 4 6; "
                         "record chord 60 64 67 71; record chord 60; record chord 60 bad; record chord 60 200; quit");
    expect(out, "Record is not enabled", "needs record on");
    expect(out, "Record channel 5 is not in a track", "needs a track");
    expect(out, "Chord has 4 notes but track 4-6 has 3 channels", "too many notes");
    expect(out, "Usage: record chord [dry] <midi> <midi>", "one note or bad token rejected");
  }

  // A chord lands on one row across the track; the cursor advances once.
  {
    const auto out = run("ch track add 4 6; record on 6; record jump 2; record chord dry 62 65; "
                         "record chord 60 64 67 instr 1 vel 90; record chord 57 60; record cursor status; "
                         "pattern print 0 2; quit");
    expect(out, "Record chord dry-run: row 0, instr 0, vel 100, ch 4=62 ch 5=65", "dry run");
    expect(out, "Recorded chord at row 0: ch 4=60 ch 5=64 ch 6=67", "chord spread over the track");
    expect(out, "Recorded chord at row 2: ch 4=57 ch 5=60", "next chord after the jump");
    expect(out, "Record cursor row: 4", "cursor advanced once per chord");
  }

  // record undo/redo take a whole chord back and forth, also after save/load.
  {
    const std::string path = "/tmp/xt_cli_record_chord_test.xtp";
    std::filesystem::remove(path);
    const auto out = run("ch track add 4 6; record on 4; record chord 60 64 67; record undo; pattern print 0 0; "
                         "record redo; save " + path + "; quit");
    const auto reloaded = run("load " + path + "; record undo; pattern print 0 0; quit");
    std::filesystem::remove(path);
    expect(out, "Undid record write at row 0, channel 6 and 2 more chord note(s)", "chord undo");
    expect(out, "Redid record write at row 0, channel 6 and 2 more chord note(s)", "chord redo");
    expect(reloaded, "Undid record write at row 0, channel 6 and 2 more chord note(s)", "chord undo after load");
  }

  if (failures != 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "cli record chord test passed\n";
  return 0;
}
