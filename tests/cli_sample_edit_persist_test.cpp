// cli_sample_edit_persist_test.cpp
// Sample edits (reverse, trim, ...) only change the sample in memory. Saving a
// song used to store just the original file path, so a reversed sample came
// back unreversed after loading. Edited samples are now written as WAVs into
// the song's bundle folder.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static std::string runScript(const std::string& script) {
    const std::string scriptPath = "./test_sample_edit_persist_script.txt";
    {
        std::ofstream sf(scriptPath);
        sf << script;
    }
    const std::string cmd = "./extracker < " + scriptPath + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};
    std::string result;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) result += buf;
    pclose(pipe);
    std::remove(scriptPath.c_str());
    return result;
}

static void writeU16(std::ofstream& f, std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); }
static void writeU32(std::ofstream& f, std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); }

// 16-bit mono WAV holding a rising ramp, so reversing it is easy to see.
static void writeRampWav(const std::string& path, std::size_t frames) {
    std::ofstream f(path, std::ios::binary);
    const std::uint32_t dataSize = static_cast<std::uint32_t>(frames * 2);
    f.write("RIFF", 4); writeU32(f, 36 + dataSize); f.write("WAVE", 4);
    f.write("fmt ", 4); writeU32(f, 16); writeU16(f, 1); writeU16(f, 1);
    writeU32(f, 44100); writeU32(f, 44100 * 2); writeU16(f, 2); writeU16(f, 16);
    f.write("data", 4); writeU32(f, dataSize);
    for (std::size_t i = 0; i < frames; ++i) {
        writeU16(f, static_cast<std::uint16_t>(static_cast<std::int16_t>(i * 100)));
    }
}

// Reads the PCM samples of a 16-bit mono WAV (finds the data chunk).
static std::vector<std::int16_t> readWav16(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto data = bytes.find("data");
    if (data == std::string::npos) return {};
    std::uint32_t size = 0;
    bytes.copy(reinterpret_cast<char*>(&size), 4, data + 4);
    std::vector<std::int16_t> out(size / 2);
    bytes.copy(reinterpret_cast<char*>(out.data()), size, data + 8);
    return out;
}

static std::string readFile(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// The stored path of SAMPLE_ENTRY <slot>, resolved against the song's directory.
static std::string sampleEntryPath(const std::string& song, int slot) {
    std::istringstream in(readFile(song));
    std::string line;
    const std::string prefix = "SAMPLE_ENTRY " + std::to_string(slot) + " ";
    while (std::getline(in, line)) {
        if (line.rfind(prefix, 0) == 0) {
            const auto close = line.rfind('"');
            const auto open = line.rfind('"', close - 1);
            return "./" + line.substr(open + 1, close - open - 1);
        }
    }
    return {};
}

int main() {
    const std::string wavPath = "./test_sample_edit_persist_ramp.wav";
    const std::string songPath = "./test_sample_edit_persist.xtp";
    const std::string resavedPath = "./test_sample_edit_persist_resaved.xtp";
    constexpr std::size_t kFrames = 64;
    writeRampWav(wavPath, kFrames);

    // Reverse the sample, save.
    runScript("sample load 3 ramp " + wavPath + "\n"
              "sample edit reverse 3\n"
              "w " + songPath + "\n"
              "quit\n");

    const std::string storedPath = sampleEntryPath(songPath, 3);
    assert(!storedPath.empty());
    assert(storedPath.find("test_sample_edit_persist_samples/") != std::string::npos);
    const auto saved = readWav16(storedPath);
    assert(saved.size() == kFrames);
    assert(saved.front() > saved.back());  // reversed ramp

    // The original file is left alone.
    const auto original = readWav16(wavPath);
    assert(original.size() == kFrames && original.front() < original.back());

    // Loading the song brings back the reversed sample, and saving again
    // (now unedited) keeps it.
    runScript("r " + songPath + "\n"
              "w " + resavedPath + "\n"
              "quit\n");
    const auto resaved = readWav16(sampleEntryPath(resavedPath, 3));
    assert(resaved == saved);

    // Restoring the source after a reload keeps the reversed data, since
    // that is what the song's file holds.
    runScript("r " + songPath + "\n"
              "sample edit reverse 3\n"
              "sample edit restore 3\n"
              "w " + resavedPath + "\n"
              "quit\n");
    assert(readWav16(sampleEntryPath(resavedPath, 3)) == saved);

    std::filesystem::remove(wavPath);
    std::filesystem::remove(songPath);
    std::filesystem::remove(resavedPath);
    std::filesystem::remove_all("./test_sample_edit_persist_samples");
    std::filesystem::remove_all("./test_sample_edit_persist_resaved_samples");
    return 0;
}
