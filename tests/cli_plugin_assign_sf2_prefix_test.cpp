// cli_plugin_assign_sf2_prefix_test.cpp — plain "sf2:<path>" id (as shown by
// `plugin list`/song files) must load via `plugin assign`, not just via
// song-file restore. Regression test for a gap where the assign dispatch
// only special-cased the ":melodic:" suffix and silently fell through to
// assignInstrument, which requires a prior `plugin scan`.
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

static const char* kSystemSf2 = "/usr/share/sounds/sf2/FluidR3_GM.sf2";

int main() {
    // 1. Nonexistent SF2 path → graceful failure, no crash
    {
        const auto out = run("plugin assign 0 sf2:/nonexistent.sf2; quit");
        assert(!out.empty());
        assert(out.find("Failed to load SF2 file") != std::string::npos);
    }

    // 2. Plain "sf2:<path>" (no ":melodic:" suffix) assigns directly,
    // without requiring a prior `plugin scan`.
    if (std::FILE* f = std::fopen(kSystemSf2, "r")) {
        std::fclose(f);
        std::string cmd = std::string("plugin assign 3 sf2:") + kSystemSf2 + "; instrument list; quit";
        const auto out = run(cmd);
        assert(out.find("Assigned SF2 instrument sf2:") != std::string::npos);
        assert(out.find("[3] sf2:") != std::string::npos);
    }

    return 0;
}
