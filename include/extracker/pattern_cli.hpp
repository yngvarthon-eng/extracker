#pragma once

#include <mutex>
#include <sstream>

#include "extracker/audio_engine.hpp"
#include "extracker/pattern_editor.hpp"
#include "extracker/sequencer.hpp"
#include "extracker/transport.hpp"

namespace extracker {

struct PatternCommandContext {
  PatternEditor& editor;
  std::mutex& stateMutex;
  Transport& transport;
  Sequencer& sequencer;
  AudioEngine& audio;
  int& playRangeFrom;
  int& playRangeTo;
  bool& playRangeActive;
  bool loopEnabled;
  bool& recordCanUndo;
  bool& recordCanRedo;
  int& recordCursorRow;
};

void handlePatternCommand(PatternCommandContext context,
                          std::istringstream& patternInput);

}  // namespace extracker
