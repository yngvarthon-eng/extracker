#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace extracker {

struct MidiEvent {
  enum class Type {
    NoteOn,
    NoteOff,
    Clock,
    Start,
    Continue,
    Stop
  };

  Type type = Type::NoteOn;
  std::uint8_t channel = 0;
  std::uint8_t note = 0;
  std::uint8_t velocity = 0;
};

class MidiInput {
public:
  using EventCallback = std::function<void(const MidiEvent&)>;

  MidiInput();
  ~MidiInput();

  bool start(EventCallback callback);
  void stop();
  bool isRunning() const;

  std::string backendName() const;
  std::string lastError() const;
  std::string endpointHint() const;

private:
  void run();

  EventCallback callback_;
  std::atomic<bool> running_;
  std::string lastError_;

#ifdef EXTRACKER_HAVE_ALSA
  struct AlsaState;
  AlsaState* alsa_;
#else
  void* alsa_;
#endif
  std::thread thread_;
};

}  // namespace extracker
