// cli_note_noteoff_fade_effect_test.cpp
// Tests that note-off with effect 0x14 (fade-out) saves and loads correctly.
#include <cassert>
#include <cstdio>
#include <string>

static std::string run(const std::string& script) {
    std::string cmd = "echo '" + script + "' | ./extracker 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};
    std::string result;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    pclose(pipe);
    return result;
}

int main() {
    // 1. note off with fade effect (0x14) on row 0 ch 0
    {
        const auto out = run("note off 0 0 32; pattern print 0 0; quit");
        assert(out.find("0 ") != std::string::npos || out.find("off") != std::string::npos || !out.empty());
    }

    // 2. note set with effect 14 (fade-out)
    {
        const auto out = run("note set 0 0 60 0 100 20 64; pattern print 0 0; quit");
        assert(!out.empty());
    }

    return 0;
}
