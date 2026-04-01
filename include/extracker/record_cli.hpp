#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <sstream>

#include "extracker/pattern_editor.hpp"
#include "extracker/record_workflow.hpp"
#include "extracker/transport.hpp"

namespace extracker {

struct RecordCommandContext {
  PatternEditor& editor;
  Transport& transport;
  std::mutex& stateMutex;
  RecordWorkflowState& recordState;
  int& recordChannel;
  int& recordCursorRow;
  bool& recordEnabled;
  bool& recordQuantizeEnabled;
  bool& recordOverdubEnabled;
  int& recordInsertJump;
  const int& midiInstrument;
  const std::function<int(int)>& chooseRecordRow;
  const std::function<void(int,
                           int,
                           int,
                           std::uint8_t,
                           std::uint32_t,
                           std::uint8_t,
                           bool,
                           std::uint8_t,
                           std::uint8_t)>& applyRecordWrite;
};

void handleRecordCommand(std::istringstream& recordInput,
                         RecordCommandContext context);

}  // namespace extracker
