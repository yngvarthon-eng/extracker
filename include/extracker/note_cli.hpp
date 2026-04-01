#pragma once

#include <mutex>
#include <sstream>

#include "extracker/pattern_editor.hpp"

namespace extracker {

void handleNoteCommand(PatternEditor& editor,
                       std::mutex& stateMutex,
                       std::istringstream& noteInput);

}  // namespace extracker
