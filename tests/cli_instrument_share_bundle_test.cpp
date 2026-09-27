// cli_instrument_share_bundle_test.cpp
// Verifies that `.xtp` songs saved by the CLI are portable: referenced
// samples and SFZ instruments (with their sibling sample files) get copied
// into a "<song>_samples"/"<song>_instruments" bundle next to the song file,
// with paths stored relative to it, so loading the song from a different
// directory (simulating "share this folder with someone else") resolves
// everything without needing the original library paths.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static std::string runScriptIn(const std::string& workDir, const std::string& script) {
    const std::string scriptPath = workDir + "/script.txt";
    FILE* sf = fopen(scriptPath.c_str(), "w");
    if (!sf) return {};
    fwrite(script.c_str(), 1, script.size(), sf);
    fclose(sf);

    const std::string extrackerAbsPath = std::filesystem::absolute("./extracker").string();
    const std::string cmd = "cd '" + workDir + "' && '" + extrackerAbsPath + "' < script.txt 2>&1";
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

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return content;
}

int main() {
    namespace fs = std::filesystem;

    const fs::path root = fs::absolute("./bundle_share_test_root").lexically_normal();
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);

    // An external "instrument library" directory, well outside the song's
    // own directory, standing in for e.g. a user's personal sample library.
    const fs::path library = root / "library";
    fs::create_directories(library);
    assert(writeTestWav((library / "kick.wav").string()));
    {
        std::ofstream sfz(library / "kit.sfz");
        sfz << "<region> sample=kick.wav lokey=0 hikey=127 pitch_keycenter=60\n";
    }

    // The "author" directory where the song is first saved.
    const fs::path authorDir = root / "author";
    fs::create_directories(authorDir);
    assert(writeTestWav((authorDir / "snare.wav").string()));

    {
        const std::string script =
            "sample load 3 mysnare snare.wav\n"
            "instrument sample 2 3\n"
            "plugin assign 4 " + (library / "kit.sfz").string() + "\n"
            "w song.xtp\n"
            "quit\n";
        const auto out = runScriptIn(authorDir.string(), script);
        assert(out.find("Assigned SFZ instrument") != std::string::npos);
        assert(out.find("Module saved to song.xtp") != std::string::npos);

        const std::string xtp = readFile((authorDir / "song.xtp").string());
        // Sample got bundled next to the song, path stored relative to it.
        assert(xtp.find("SAMPLE_ENTRY 3 \"mysnare\" \"song_samples/") != std::string::npos);
        assert(fs::exists(authorDir / "song_samples"));
        // SFZ instrument (and its sibling kick.wav) got bundled too.
        assert(xtp.find("INSTRUMENT_ASSIGN 4 \"song_instruments/") != std::string::npos);
        assert(xtp.find("kit.sfz") != std::string::npos);
        assert(fs::exists(authorDir / "song_instruments"));
        bool foundBundledWav = false;
        for (const auto& entry : fs::recursive_directory_iterator(authorDir / "song_instruments")) {
            if (entry.path().filename() == "kick.wav") foundBundledWav = true;
        }
        assert(foundBundledWav);
    }

    // Simulate "share the folder": copy just the song + its two bundle
    // directories into a fresh directory that has no access to `library`.
    const fs::path recipientDir = root / "recipient";
    fs::create_directories(recipientDir);
    fs::copy(authorDir / "song.xtp", recipientDir / "song.xtp");
    fs::copy(authorDir / "song_samples", recipientDir / "song_samples", fs::copy_options::recursive);
    fs::copy(authorDir / "song_instruments", recipientDir / "song_instruments", fs::copy_options::recursive);

    {
        const std::string script =
            "r song.xtp\n"
            "plugin status\n"
            "quit\n";
        const auto out = runScriptIn(recipientDir.string(), script);
        assert(out.find("Module loaded from song.xtp") != std::string::npos);
        assert(out.find("could not restore instrument") == std::string::npos);
        // Instrument 4 (the SFZ) should resolve to the recipient's own copy,
        // not the original library path.
        assert(out.find(recipientDir.string()) != std::string::npos);
        assert(out.find("library/kit.sfz") == std::string::npos);
    }

    fs::remove_all(root, ec);
    return 0;
}
