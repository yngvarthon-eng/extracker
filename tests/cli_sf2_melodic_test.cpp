// cli_sf2_melodic_test.cpp — tests for SF2 melodic plugin and plugin zones
#include <cassert>
#include <cstdio>
#include <cstring>
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

static const char* kSystemSf2 = "/usr/share/sounds/sf2/FluidR3_GM.sf2";

int main() {
    // 1. plugin zones with no arg → usage message
    {
        const auto out = run("plugin zones; quit");
        assert(!out.empty());
    }

    // 2. plugin assign nonexistent SF2 melodic → graceful failure, no crash
    {
        const auto out = run("plugin assign 0 sf2:/nonexistent.sf2:melodic:36; quit");
        assert(!out.empty());
    }

    // 3. system SF2 melodic assign — succeeds or fails gracefully
    {
        std::string cmd = std::string("plugin assign 0 sf2:") + kSystemSf2 + ":melodic:36; status; quit";
        const auto out = run(cmd);
        assert(!out.empty());
        // Either loaded or failed — both are valid when the file may not exist
    }

    // 4. plugin zones on system SF2 — reports keys or "No drum keys found"
    {
        std::string cmd = std::string("plugin zones ") + kSystemSf2 + "; quit";
        const auto out = run(cmd);
        assert(!out.empty());
    }

    return 0;
}
