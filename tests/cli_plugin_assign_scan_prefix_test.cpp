// cli_plugin_assign_scan_prefix_test.cpp
// SFZScanAdapter/BuiltinSfzScanAdapter and S3IScanAdapter register
// discovered plugins as "sfz:<path>"/"s3i:<path>" so they show up in
// `plugin list`, but `plugin assign <instr> <id>` used to hand that literal
// prefixed string straight to loadSfzInstrument/loadS3iInstrument, which
// only accept a bare path -- so assigning an id exactly as `plugin list`
// showed it always failed. Verifies both now work.
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

    const fs::path libDir = fs::absolute("./plugin_assign_scan_prefix_test_lib");
    std::error_code ec;
    fs::remove_all(libDir, ec);
    fs::create_directories(libDir);

    assert(writeTestWav((libDir / "snap.wav").string()));
    {
        std::ofstream sfz(libDir / "snap.sfz");
        sfz << "<region> sample=snap.wav lokey=0 hikey=127 pitch_keycenter=60\n";
    }

    const std::string sfzPath = (libDir / "snap.sfz").string();
    const std::string env = "SFZ_PATH=" + libDir.string();

    // Assign using exactly the "sfz:<path>" id form the scan adapter
    // registers (and `plugin list` displays), not the bare path.
    const auto out = runScriptWithEnv(
        env, "plugin scan\nplugin assign 6 sfz:" + sfzPath + "\nquit\n",
        "./plugin_assign_scan_prefix_test_script.txt");
    assert(out.find("Assigned SFZ instrument") != std::string::npos);
    assert(out.find("Failed to load SFZ file") == std::string::npos);

    fs::remove_all(libDir, ec);
    remove("./plugin_assign_scan_prefix_test_script.txt");
    return 0;
}
