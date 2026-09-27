#pragma once

#include <mutex>
#include <sstream>

#include "extracker/audio_engine.hpp"
#include "extracker/channel_layout.hpp"
#include "extracker/channel_manager.hpp"
#include "extracker/module.hpp"
#include "extracker/record_workflow.hpp"
#include "extracker/sequencer.hpp"

namespace extracker {

struct ChannelCommandContext {
  ChannelManager& channels;
  Module& module;
  Sequencer& sequencer;
  AudioEngine& audio;
  RecordWorkflowState& record;
  ChannelLayoutHistory& history;
  std::mutex& stateMutex;
};

void handleChannelCommand(std::istringstream& input, ChannelCommandContext context);

}  // namespace extracker
