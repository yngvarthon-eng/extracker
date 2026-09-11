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
    // 1. Default: all channels at 100%
    {
        const auto out = run("channel vol; quit");
        assert(out.find("ch  0: 100%") != std::string::npos);
        assert(out.find("ch 15: 100%") != std::string::npos);
    }

    // 2. Set channel 0 to 50%
    {
        const auto out = run("channel vol 0 50; channel vol 0; quit");
        assert(out.find("Channel 0 volume set to 50%") != std::string::npos);
        assert(out.find("Channel 0 volume: 50%") != std::string::npos);
    }

    // 3. Set channel 3 to 0% (mute)
    {
        const auto out = run("channel vol 3 0; channel vol 3; quit");
        assert(out.find("Channel 3 volume set to 0%") != std::string::npos);
        assert(out.find("Channel 3 volume: 0%") != std::string::npos);
    }

    // 4. Boost to 150%
    {
        const auto out = run("channel vol 1 150; channel vol 1; quit");
        assert(out.find("Channel 1 volume set to 150%") != std::string::npos);
        assert(out.find("Channel 1 volume: 150%") != std::string::npos);
    }

    // 5. Invalid channel index
    {
        const auto out = run("channel vol 99 50; quit");
        assert(out.find("Invalid channel") != std::string::npos);
    }

    // 6. Out of range value
    {
        const auto out = run("channel vol 0 999; quit");
        assert(out.find("out of range") != std::string::npos);
    }

    // 7. Alias 'ch vol' works the same
    {
        const auto out = run("ch vol 2 75; ch vol 2; quit");
        assert(out.find("Channel 2 volume set to 75%") != std::string::npos);
        assert(out.find("Channel 2 volume: 75%") != std::string::npos);
    }

    // 8. Help
    {
        const auto out = run("channel help; quit");
        assert(out.find("vol") != std::string::npos);
    }

    return 0;
}
