#pragma once

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace extracker {

class Module;

struct ModuleCommandContext {
  Module& module;
  std::atomic<bool>* songModeEnabled = nullptr;
  std::atomic<std::size_t>* songPlaybackPosition = nullptr;
};

bool handleModuleCommand(const std::string& command,
                         const std::vector<std::string>& tokens,
                         ModuleCommandContext context);

}  // namespace extracker
