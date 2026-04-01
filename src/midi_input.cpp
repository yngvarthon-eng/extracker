#include "extracker/midi_input.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#ifdef EXTRACKER_HAVE_ALSA
#include <alsa/asoundlib.h>
#endif

namespace extracker {

#ifdef EXTRACKER_HAVE_ALSA
struct MidiInput::AlsaState {
  snd_seq_t* seq = nullptr;
  int inPort = -1;
  int clientId = -1;
};
#endif

MidiInput::MidiInput()
    : callback_(),
      running_(false),
      lastError_(),
      alsa_(nullptr),
      thread_() {
#ifdef EXTRACKER_HAVE_ALSA
  alsa_ = new AlsaState();
#endif
}

MidiInput::~MidiInput() {
  stop();
#ifdef EXTRACKER_HAVE_ALSA
  delete alsa_;
  alsa_ = nullptr;
#endif
}

bool MidiInput::start(EventCallback callback) {
  if (running_.load()) {
    return true;
  }

  callback_ = std::move(callback);
  lastError_.clear();

#ifdef EXTRACKER_HAVE_ALSA
  if (callback_ == nullptr) {
    lastError_ = "MIDI callback not configured";
    return false;
  }

  int result = snd_seq_open(&alsa_->seq, "default", SND_SEQ_OPEN_INPUT, 0);
  if (result < 0 || alsa_->seq == nullptr) {
    lastError_ = "Failed to open ALSA sequencer input";
    return false;
  }

  snd_seq_set_client_name(alsa_->seq, "exTracker MIDI Input");
  alsa_->clientId = snd_seq_client_id(alsa_->seq);

  alsa_->inPort = snd_seq_create_simple_port(
      alsa_->seq,
      "exTracker In",
      SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
      SND_SEQ_PORT_TYPE_APPLICATION);

  if (alsa_->inPort < 0) {
    lastError_ = "Failed to create ALSA sequencer input port";
    snd_seq_close(alsa_->seq);
    alsa_->seq = nullptr;
    return false;
  }

  snd_seq_nonblock(alsa_->seq, 1);

  running_.store(true);
  thread_ = std::thread([this]() { run(); });
  return true;
#else
  (void)callback;
  lastError_ = "ALSA MIDI support not built";
  return false;
#endif
}

void MidiInput::stop() {
  running_.store(false);
  if (thread_.joinable()) {
    thread_.join();
  }

#ifdef EXTRACKER_HAVE_ALSA
  if (alsa_ != nullptr && alsa_->seq != nullptr) {
    snd_seq_close(alsa_->seq);
    alsa_->seq = nullptr;
    alsa_->inPort = -1;
    alsa_->clientId = -1;
  }
#endif
}

bool MidiInput::isRunning() const {
  return running_.load();
}

std::string MidiInput::backendName() const {
#ifdef EXTRACKER_HAVE_ALSA
  return "ALSA Sequencer";
#else
  return "Unavailable";
#endif
}

std::string MidiInput::lastError() const {
  return lastError_;
}

std::string MidiInput::endpointHint() const {
#ifdef EXTRACKER_HAVE_ALSA
  if (alsa_ == nullptr || alsa_->clientId < 0 || alsa_->inPort < 0) {
    return "MIDI input not active";
  }
  return "Connect controller with: aconnect <source-client>:<source-port> " +
      std::to_string(alsa_->clientId) + ":" + std::to_string(alsa_->inPort);
#else
  return "No MIDI backend available";
#endif
}

void MidiInput::run() {
#ifdef EXTRACKER_HAVE_ALSA
  while (running_.load()) {
    if (alsa_ == nullptr || alsa_->seq == nullptr) {
      break;
    }

    snd_seq_event_t* event = nullptr;
    int result = snd_seq_event_input(alsa_->seq, &event);
    if (result >= 0 && event != nullptr) {
      MidiEvent midiEvent;
      bool handled = false;

      if (event->type == SND_SEQ_EVENT_NOTEON) {
        midiEvent.channel = static_cast<std::uint8_t>(event->data.note.channel & 0x0F);
        midiEvent.note = static_cast<std::uint8_t>(std::clamp<int>(event->data.note.note, 0, 127));
        midiEvent.velocity = static_cast<std::uint8_t>(std::clamp<int>(event->data.note.velocity, 0, 127));
        midiEvent.type = midiEvent.velocity == 0 ? MidiEvent::Type::NoteOff : MidiEvent::Type::NoteOn;
        handled = true;
      } else if (event->type == SND_SEQ_EVENT_NOTEOFF) {
        midiEvent.channel = static_cast<std::uint8_t>(event->data.note.channel & 0x0F);
        midiEvent.note = static_cast<std::uint8_t>(std::clamp<int>(event->data.note.note, 0, 127));
        midiEvent.velocity = static_cast<std::uint8_t>(std::clamp<int>(event->data.note.velocity, 0, 127));
        midiEvent.type = MidiEvent::Type::NoteOff;
        handled = true;
      } else if (event->type == SND_SEQ_EVENT_CLOCK) {
        midiEvent.type = MidiEvent::Type::Clock;
        handled = true;
      } else if (event->type == SND_SEQ_EVENT_START) {
        midiEvent.type = MidiEvent::Type::Start;
        handled = true;
      } else if (event->type == SND_SEQ_EVENT_CONTINUE) {
        midiEvent.type = MidiEvent::Type::Continue;
        handled = true;
      } else if (event->type == SND_SEQ_EVENT_STOP) {
        midiEvent.type = MidiEvent::Type::Stop;
        handled = true;
      }

      if (handled && callback_ != nullptr) {
        callback_(midiEvent);
      }
      continue;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
#endif
}

}  // namespace extracker
