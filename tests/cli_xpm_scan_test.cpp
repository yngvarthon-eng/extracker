// cli_xpm_scan_test.cpp
// Verifies `.xpm` (Akai-style multi-zone keygroup) instruments are
// discovered by `plugin scan` via XPM_PATH, appear in `plugin list` under
// their bare file path (not a synthetic prefix -- unlike sf2:/sfz:/s3i:,
// there's no separate resolution scheme to keep in sync), and can be
// assigned directly with `plugin assign`.
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static std::string runScriptWithEnv(const std::string& env, const std::string& script,
                                     const std::string& scratchName) {
    FILE* sf = fopen(scratchName.c_str(), "w");
    if (!sf) return {};
    fwrite(script.c_str(), 1, script.size(), sf);
    fclose(sf);
    const std::string cmd = env + " ./extracker < " + scratchName + " 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};
    std::string result;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    pclose(pipe);
    return result;
}

static bool writeTestWav(const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t dataSize = 256;
    const uint32_t sampleRate = 44100;
    const uint16_t channels = 1, bitsPerSample = 16, blockAlign = 2, audioFmt = 1;
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t chunkSize = 36 + dataSize;
    fwrite("RIFF", 1, 4, f); fwrite(&chunkSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    const uint32_t fmtSize = 16;
    fwrite(&fmtSize, 4, 1, f); fwrite(&audioFmt, 2, 1, f); fwrite(&channels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f); fwrite(&bitsPerSample, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataSize, 4, 1, f);
    for (uint32_t i = 0; i < dataSize / 2; ++i) { uint16_t s = 0; fwrite(&s, 2, 1, f); }
    fclose(f);
    return true;
}

int main() {
    namespace fs = std::filesystem;

    const fs::path libDir = fs::absolute("./xpm_scan_test_lib");
    std::error_code ec;
    fs::remove_all(libDir, ec);
    fs::create_directories(libDir);

    assert(writeTestWav((libDir / "pluck.wav").string()));
    {
        std::ofstream xpm(libDir / "pluck.xpm");
        xpm << "<Instrument number=\"0\">\n"
            << "<LowNote>0</LowNote>\n"
            << "<HighNote>127</HighNote>\n"
            << "<Layer number=\"0\">\n"
            << "<Active>True</Active>\n"
            << "<SampleName>pluck</SampleName>\n"
            << "</Layer>\n"
            << "</Instrument>\n";
    }

    const std::string env = "XPM_PATH=" + libDir.string();
    const std::string xpmPath = (libDir / "pluck.xpm").string();

    // Discovery: `plugin scan` should find it, and `plugin list` should show
    // its bare path (no synthetic prefix).
    {
        const auto out = runScriptWithEnv(env, "plugin scan\nplugin list\nquit\n",
                                           "./xpm_scan_test_scan.txt");
        assert(out.find("Scan found") != std::string::npos);
        assert(out.find(xpmPath) != std::string::npos);
    }

    // Direct assignment by the same bare path `plugin list` shows.
    {
        const auto out = runScriptWithEnv(env, "plugin assign 6 " + xpmPath + "\nquit\n",
                                           "./xpm_scan_test_assign.txt");
        assert(out.find("Assigned XPM keygroup") != std::string::npos);
    }

    fs::remove_all(libDir, ec);
    remove("./xpm_scan_test_scan.txt");
    remove("./xpm_scan_test_assign.txt");
    return 0;
}
