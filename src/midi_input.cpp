#include "extracker/midi_input.hpp"

#include <algorithm>
#include <chrono>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#endif

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

#ifdef _WIN32
struct MidiInput::WinMidiState {
  HMIDIIN handle = nullptr;
  EventCallback callback;
};

static void CALLBACK winMidiProc(HMIDIIN, UINT msg, DWORD_PTR instance,
                                  DWORD_PTR param1, DWORD_PTR) {
  if (msg != MIM_DATA) return;
  auto* state = reinterpret_cast<MidiInput::WinMidiState*>(instance);
  if (!state || !state->callback) return;

  const auto status   = static_cast<std::uint8_t>(param1 & 0xFF);
  const auto data1    = static_cast<std::uint8_t>((param1 >> 8) & 0x7F);
  const auto data2    = static_cast<std::uint8_t>((param1 >> 16) & 0x7F);
  const std::uint8_t type    = status & 0xF0;
  const std::uint8_t channel = status & 0x0F;

  MidiEvent ev;
  ev.channel = channel;

  if (type == 0x90 && data2 > 0) {
    ev.type = MidiEvent::Type::NoteOn;
    ev.note = data1;
    ev.velocity = data2;
  } else if (type == 0x80 || (type == 0x90 && data2 == 0)) {
    ev.type = MidiEvent::Type::NoteOff;
    ev.note = data1;
    ev.velocity = data2;
  } else if (type == 0xB0) {
    ev.type = MidiEvent::Type::ControlChange;
    ev.controller = data1;
    ev.value = data2;
  } else if (type == 0xF0) {
    switch (status) {
      case 0xF8: ev.type = MidiEvent::Type::Clock;    break;
      case 0xFA: ev.type = MidiEvent::Type::Start;    break;
      case 0xFB: ev.type = MidiEvent::Type::Continue; break;
      case 0xFC: ev.type = MidiEvent::Type::Stop;     break;
      default: return;
    }
  } else {
    return;
  }

  state->callback(ev);
}
#endif  // _WIN32

MidiInput::MidiInput()
    : callback_(),
      running_(false),
      lastError_(),
      alsa_(nullptr),
#ifdef _WIN32
      winMidi_(nullptr),
#endif
      thread_() {
#ifdef EXTRACKER_HAVE_ALSA
  alsa_ = new AlsaState();
#endif
#ifdef _WIN32
  winMidi_ = new WinMidiState();
#endif
}

MidiInput::~MidiInput() {
  stop();
#ifdef EXTRACKER_HAVE_ALSA
  delete alsa_;
  alsa_ = nullptr;
#endif
#ifdef _WIN32
  delete winMidi_;
  winMidi_ = nullptr;
#endif
}

bool MidiInput::start(EventCallback callback) {
  if (running_.load()) {
    return true;
  }

  callback_ = std::move(callback);
  lastError_.clear();

#if defined(_WIN32)
  if (midiInGetNumDevs() == 0) {
    lastError_ = "No MIDI input devices found";
    return false;
  }

  winMidi_->callback = callback_;
  MMRESULT result = midiInOpen(&winMidi_->handle, 0,
                               reinterpret_cast<DWORD_PTR>(winMidiProc),
                               reinterpret_cast<DWORD_PTR>(winMidi_),
                               CALLBACK_FUNCTION);
  if (result != MMSYSERR_NOERROR) {
    winMidi_->handle = nullptr;
    lastError_ = "Failed to open MIDI input device";
    return false;
  }

  midiInStart(winMidi_->handle);
  running_.store(true);
  thread_ = std::thread([this]() { run(); });
  return true;

#elif defined(EXTRACKER_HAVE_ALSA)
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
  lastError_ = "No MIDI backend available";
  return false;
#endif
}

void MidiInput::stop() {
  running_.store(false);
  if (thread_.joinable()) {
    thread_.join();
  }

#ifdef _WIN32
  if (winMidi_ != nullptr && winMidi_->handle != nullptr) {
    midiInStop(winMidi_->handle);
    midiInClose(winMidi_->handle);
    winMidi_->handle = nullptr;
    winMidi_->callback = nullptr;
  }
#endif

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
#if defined(_WIN32)
  return "Windows MIDI";
#elif defined(EXTRACKER_HAVE_ALSA)
  return "ALSA Sequencer";
#else
  return "Unavailable";
#endif
}

std::string MidiInput::lastError() const {
  return lastError_;
}

std::string MidiInput::endpointHint() const {
#if defined(_WIN32)
  if (winMidi_ == nullptr || winMidi_->handle == nullptr) {
    return "MIDI input not active";
  }
  UINT numDevs = midiInGetNumDevs();
  if (numDevs == 0) return "No MIDI input devices available";
  MIDIINCAPSA caps{};
  if (midiInGetDevCapsA(0, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
    return std::string("Connected to: ") + caps.szPname;
  }
  return "MIDI input active (device 0)";
#elif defined(EXTRACKER_HAVE_ALSA)
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
#if defined(_WIN32)
  // winmm delivers MIDI via the winMidiProc callback on its own thread;
  // this thread just keeps running_ alive for isRunning() callers.
  while (running_.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
#elif defined(EXTRACKER_HAVE_ALSA)
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
      } else if (event->type == SND_SEQ_EVENT_CONTROLLER) {
        midiEvent.channel = static_cast<std::uint8_t>(event->data.control.channel & 0x0F);
        midiEvent.controller = static_cast<std::uint8_t>(std::clamp<int>(event->data.control.param, 0, 127));
        midiEvent.value = static_cast<std::uint8_t>(std::clamp<int>(event->data.control.value, 0, 127));
        midiEvent.type = MidiEvent::Type::ControlChange;
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
