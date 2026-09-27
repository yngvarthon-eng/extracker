#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

#ifndef EXTRACKER_VST3_TEST_PLUGIN_PATH
#error "EXTRACKER_VST3_TEST_PLUGIN_PATH must be defined"
#endif

// Test plugin class ID matches kTestClassId in vst3_test_plugin.cpp
static constexpr const char* kTestPluginId = "vst3.0102030405060708090A0B0C0D0E0F10";

static std::string runCli(const std::string& env, const std::string& commands,
                          int* exitStatus = nullptr) {
    // env must prefix the binary, not printf, to reach the child process.
    const std::string cmd = "printf '" + commands + "' | " + env + " ./extracker";
    std::array<char, 4096> buf{};
    std::string output;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) { std::cerr << "popen failed\n"; return {}; }
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        output += buf.data();
    }
    const int status = pclose(pipe);
    if (exitStatus) *exitStatus = status;
    return output;
}

int main() {
    if (!std::filesystem::exists("./extracker")) {
        std::cerr << "extracker binary not found\n";
        return 1;
    }

    // Put the .vst3 flat file (the .so itself) into a temp directory and point
    // VST3_PATH at it.  vst3ResolveSoPath accepts a .vst3 regular file directly.
    const std::string pluginSoPath = EXTRACKER_VST3_TEST_PLUGIN_PATH;
    if (!std::filesystem::exists(pluginSoPath)) {
        std::cerr << "VST3 test plugin .so not found at " << pluginSoPath << '\n';
        return 1;
    }

    // Create bundle directory: tmpdir/ExTrackerTestSynth.vst3/Contents/x86_64-linux/plugin.so
    const auto tmpBase = std::filesystem::temp_directory_path() / "extracker_vst3_test";
    const auto bundleDir = tmpBase / "ExTrackerTestSynth.vst3" / "Contents" / "x86_64-linux";
    std::filesystem::create_directories(bundleDir);
    const auto linkTarget = bundleDir / "ExTrackerTestSynth.so";
    std::filesystem::remove(linkTarget);
    std::filesystem::copy_file(pluginSoPath, linkTarget);

    const std::string vst3Path = tmpBase.string();
    const std::string envPrefix = "VST3_PATH=" + vst3Path + " LV2_PATH=/dev/null ";

    // 1 — plugin scan should discover the test plugin; plugin list should show it
    {
        const std::string out = runCli(envPrefix, "plugin scan\\nplugin list\\nquit\\n");
        if (out.find(kTestPluginId) == std::string::npos) {
            std::cerr << "Expected plugin ID '" << kTestPluginId << "' in plugin list output\n" << out;
            std::filesystem::remove_all(tmpBase);
            return 1;
        }
        if (out.find("plugin(s) available") == std::string::npos &&
            out.find("Discovered plugins") == std::string::npos) {
            std::cerr << "Expected plugin availability info in output\n" << out;
            std::filesystem::remove_all(tmpBase);
            return 1;
        }
    }

    // 2 — plugin load + assign + list
    {
        const std::string commands =
            std::string("plugin scan\\n") +
            "plugin load " + kTestPluginId + "\\n" +
            "plugin assign 1 " + kTestPluginId + "\\n" +
            "plugin list\\n" +
            "quit\\n";
        const std::string out = runCli(envPrefix, commands);
        if (out.find("Instrument 1") == std::string::npos &&
            out.find("instrument 1") == std::string::npos) {
            std::cerr << "Expected instrument 1 assignment in plugin list\n" << out;
            std::filesystem::remove_all(tmpBase);
            return 1;
        }
    }

    // 3 — plugin list with no scan should still show builtins
    {
        const std::string out = runCli(envPrefix, "plugin list\\nquit\\n");
        if (out.find("builtin.sine") == std::string::npos &&
            out.find("Plugins available") == std::string::npos &&
            out.find("available") == std::string::npos) {
            std::cerr << "Expected builtin plugin info in plugin list\n" << out;
            std::filesystem::remove_all(tmpBase);
            return 1;
        }
    }

    std::filesystem::remove_all(tmpBase);
    return 0;
}
