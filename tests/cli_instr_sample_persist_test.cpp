// cli_instr_sample_persist_test.cpp
// Verifies that instrument↔sample-slot binding is saved and restored on load.
// Before this fix, assignSampleSlotToInstrument() was never written to the file,
// so reloading a song with builtin.sample instruments silenced them.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static std::string runScript(const std::string& script) {
    std::string cmd = "echo '" + script + "' | ./extracker 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};
    std::string result;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    pclose(pipe);
    return result;
}

static std::string runFile(const std::string& scriptPath) {
    std::string cmd = "./extracker < " + scriptPath + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};
    std::string result;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    pclose(pipe);
    return result;
}

// Write a minimal WAV file (44 bytes header + 256 bytes silence) for testing.
static bool writeTestWav(const std::string& path) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t dataSize = 256;
    const uint32_t sampleRate = 44100;
    const uint16_t channels = 1, bitsPerSample = 16, blockAlign = 2, audioFmt = 1;
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t chunkSize = 36 + dataSize;
    // RIFF header
    fwrite("RIFF", 1, 4, f); fwrite(&chunkSize, 4, 1, f);
    fwrite("WAVE", 1, 4, f);
    // fmt chunk
    fwrite("fmt ", 1, 4, f);
    const uint32_t fmtSize = 16;
    fwrite(&fmtSize, 4, 1, f); fwrite(&audioFmt, 2, 1, f); fwrite(&channels, 2, 1, f);
    fwrite(&sampleRate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&blockAlign, 2, 1, f); fwrite(&bitsPerSample, 2, 1, f);
    // data chunk
    fwrite("data", 1, 4, f); fwrite(&dataSize, 4, 1, f);
    for (uint32_t i = 0; i < dataSize / 2; ++i) { uint16_t s = 0; fwrite(&s, 2, 1, f); }
    fclose(f);
    return true;
}

int main() {
    // Paths for temp files — use build dir (cwd when tests run)
    const std::string wavPath = "./test_persist_sample.wav";
    const std::string songPath = "./test_persist_sample.xtp";

    // Create a minimal silent WAV
    assert(writeTestWav(wavPath));

    // 1. Save: load WAV into sample slot 3, assign to instrument 2, save song
    {
        const std::string script =
            "sample load 3 testkick " + wavPath + "\n"
            "instrument sample 2 3\n"
            "w " + songPath + "\n"
            "quit\n";
        // Write script to a temp file so we can use stdin redirect
        FILE* sf = fopen("./test_persist_script_save.txt", "w");
        assert(sf);
        fwrite(script.c_str(), 1, script.size(), sf);
        fclose(sf);
        const auto out = runFile("./test_persist_script_save.txt");
        assert(!out.empty());
        // Verify the song file contains INSTR_SAMPLE_SLOT
        FILE* xtp = fopen(songPath.c_str(), "r");
        assert(xtp);
        std::string content;
        char buf[256];
        while (fgets(buf, sizeof(buf), xtp)) content += buf;
        fclose(xtp);
        assert(content.find("INSTR_SAMPLE_SLOT") != std::string::npos);
        assert(content.find("INSTR_SAMPLE_SLOT 2 3") != std::string::npos);
        assert(content.find("SAMPLE_ENTRY 3") != std::string::npos);
    }

    // 2. Load: reload the saved song, check instrument 2 has sample slot 3.
    // `status` only ever prints instruments 0/1, so use `plugin status` to
    // see the rest of the assignment table.
    {
        const std::string script =
            "r " + songPath + "\n"
            "plugin status\n"
            "quit\n";
        FILE* sf = fopen("./test_persist_script_load.txt", "w");
        assert(sf);
        fwrite(script.c_str(), 1, script.size(), sf);
        fclose(sf);
        const auto out = runFile("./test_persist_script_load.txt");
        // Instrument 2 should show builtin.sample (the sample plugin)
        assert(out.find("2: builtin.sample") != std::string::npos);
    }

    // Cleanup
    remove(wavPath.c_str());
    remove(songPath.c_str());
    remove("./test_persist_script_save.txt");
    remove("./test_persist_script_load.txt");

    return 0;
}
