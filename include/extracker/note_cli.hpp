#pragma once

#include <mutex>
#include <sstream>

#include "extracker/channel_manager.hpp"
#include "extracker/pattern_editor.hpp"

namespace extracker {

void handleNoteCommand(PatternEditor& editor,
                       std::mutex& stateMutex,
                       std::istringstream& noteInput,
                       const ChannelManager* channels = nullptr);

}  // namespace extracker
