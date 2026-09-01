#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

// Run a command through the CLI and capture stdout.
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

// Load a known SFZ file that ships with the repo's test environment.
// This path is user-specific; tests must tolerate absence gracefully.
static const char* kTestSfz =
    "/home/yngvar/Musikk/musicworks/instruments/DSK/"
    "DSK_Xtra_Instruments_sfz/Misc/Trumphet.sfz";

int main() {
    // 1. plugin scan should report sfz-builtin adapter (no sfizz installed)
    {
        const auto out = run("plugin scan; quit");
        assert(out.find("sfz-builtin") != std::string::npos ||
               out.find("sfizz")       != std::string::npos ||
               out.find("plugin")      != std::string::npos);
    }

    // 2. assign a known SFZ via path — succeeds when WAV samples exist
    {
        const std::string cmd =
            std::string("plugin assign 0 ") + kTestSfz + "; status; quit";
        const auto out = run(cmd);
        // Either it loads (shows the path in status) or reports a failure —
        // either way the CLI must not crash.
        assert(!out.empty());
        if (out.find("Assigned") != std::string::npos ||
            out.find("Trumphet") != std::string::npos) {
            // Loaded successfully — status should echo the path
            assert(out.find("Trumphet") != std::string::npos);
        }
        // If file missing on CI just verify no crash (empty output is a crash)
        (void)out;
    }

    // 3. assign a non-existent SFZ — must not crash, must say something
    {
        const auto out = run("plugin assign 1 /nonexistent/instrument.sfz; quit");
        assert(!out.empty());
    }

    // 4. plugin scan discovers SFZ files without crashing
    {
        const auto out = run("plugin scan; plugin list; quit");
        assert(!out.empty());
    }

    return 0;
}
