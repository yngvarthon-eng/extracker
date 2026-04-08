#include "extracker/sequencer.hpp"

#include <algorithm>
#include <cmath>

#include "extracker/audio_engine.hpp"
#include "extracker/pattern_editor.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/transport.hpp"

namespace extracker {

Sequencer::Sequencer()
    : lastObservedRow_(0),
      lastObservedTickCount_(0),
      hasObservedRow_(false),
      dispatchCount_(0),
      activeNotes_{} {}

void Sequencer::reset() {
  lastObservedRow_ = 0;
  lastObservedTickCount_ = 0;
  hasObservedRow_ = false;
  dispatchCount_ = 0;
  activeNotes_.clear();
  currentRowNotes_.clear();
  effectMemoryByChannel_.clear();
}

void Sequencer::update(
  const PatternEditor& pattern,
  Transport& transport,
  AudioEngine& audioEngine,
  PluginHost& pluginHost,
  const std::vector<bool>* mutedChannels) {
  auto isChannelMuted = [mutedChannels](std::size_t channel) {
    return mutedChannels != nullptr && channel < mutedChannels->size() && (*mutedChannels)[channel];
  };

  std::uint32_t row = transport.currentRow();
  std::uint64_t tickCount = transport.tickCount();
  std::uint32_t ticksPerRow = std::max<std::uint32_t>(transport.ticksPerRow(), 1);
  std::uint32_t ticksIntoRow = static_cast<std::uint32_t>(tickCount % ticksPerRow);

  bool rowChanged = !hasObservedRow_ || row != lastObservedRow_;
  if (rowChanged) {
    std::uint32_t pendingJumpRow = row;
    bool hasPendingJump = false;

    hasObservedRow_ = true;
    lastObservedRow_ = row;
    dispatchCount_ += 1;

    if (effectMemoryByChannel_.size() != pattern.channels()) {
      effectMemoryByChannel_.assign(pattern.channels(), std::array<std::uint8_t, 16>{});
    }

    std::vector<RowNote> rowNotes;
    for (std::size_t channel = 0; channel < pattern.channels(); ++channel) {
      if (isChannelMuted(channel)) {
        continue;
      }

      std::uint8_t effectCommand = pattern.effectCommandAt(static_cast<int>(row), static_cast<int>(channel));
      std::uint8_t effectValue = pattern.effectValueAt(static_cast<int>(row), static_cast<int>(channel));

      if (effectCommand < effectMemoryByChannel_[channel].size()) {
        if (effectValue == 0) {
          effectValue = effectMemoryByChannel_[channel][effectCommand];
        } else {
          effectMemoryByChannel_[channel][effectCommand] = effectValue;
        }
      }

      if ((effectCommand == 0x05 || effectCommand == 0x06) && effectValue == 0) {
        effectValue = effectMemoryByChannel_[channel][0x0A];
      }
      if ((effectCommand == 0x05 || effectCommand == 0x06) && effectValue != 0) {
        effectMemoryByChannel_[channel][0x0A] = effectValue;
      }

      switch (effectCommand) {
        case 0x0F:
          if (effectValue > 0 && effectValue < 32) {
            transport.setTicksPerRow(effectValue);
          } else if (effectValue >= 32) {
            transport.setTempoBpm(static_cast<double>(effectValue));
          }
          break;
        case 0x0B:
          hasPendingJump = true;
          pendingJumpRow = effectValue;
          break;
        case 0x0D:
          hasPendingJump = true;
          pendingJumpRow = effectValue;
          break;
        default:
          break;
      }

      int note = pattern.noteAt(static_cast<int>(row), static_cast<int>(channel));
      if (note < 0) {
        continue;
      }

      RowNote key;
      key.midiNote = note;
      key.channel = channel;
      key.instrument = pattern.instrumentAt(static_cast<int>(row), static_cast<int>(channel));
      key.sample = pattern.sampleAt(static_cast<int>(row), static_cast<int>(channel));

      int existingIndex = findRowNoteIndex(rowNotes, key);
      std::uint32_t gate = pattern.gateTicksAt(static_cast<int>(row), static_cast<int>(channel));
      std::uint8_t velocity = pattern.velocityAt(static_cast<int>(row), static_cast<int>(channel));
      bool retrigger = pattern.retriggerAt(static_cast<int>(row), static_cast<int>(channel));

      if (effectCommand == 0x0C) {
        velocity = std::clamp<std::uint8_t>(effectValue, 1, 127);
      }

      int volumeSlideDelta = 0;
      if (effectCommand == 0x0A || effectCommand == 0x05 || effectCommand == 0x06) {
        int up = static_cast<int>((effectValue >> 4) & 0x0F);
        int down = static_cast<int>(effectValue & 0x0F);
        volumeSlideDelta = up - down;
      }

      int slideUp = effectCommand == 0x01 ? static_cast<int>(effectValue) : 0;
      int slideDown = effectCommand == 0x02 ? static_cast<int>(effectValue) : 0;
      int tonePortamento = 0;
      if (effectCommand == 0x03) {
        tonePortamento = static_cast<int>(effectValue);
      } else if (effectCommand == 0x05) {
        tonePortamento = static_cast<int>(effectMemoryByChannel_[channel][0x03]);
      }

      std::uint8_t arpeggioX = effectCommand == 0x00 ? static_cast<std::uint8_t>((effectValue >> 4) & 0x0F) : 0;
      std::uint8_t arpeggioY = effectCommand == 0x00 ? static_cast<std::uint8_t>(effectValue & 0x0F) : 0;
      std::uint8_t vibratoSpeed = 0;
      std::uint8_t vibratoDepth = 0;
      if (effectCommand == 0x04) {
        vibratoSpeed = static_cast<std::uint8_t>((effectValue >> 4) & 0x0F);
        vibratoDepth = static_cast<std::uint8_t>(effectValue & 0x0F);
      } else if (effectCommand == 0x06) {
        std::uint8_t remembered = effectMemoryByChannel_[channel][0x04];
        vibratoSpeed = static_cast<std::uint8_t>((remembered >> 4) & 0x0F);
        vibratoDepth = static_cast<std::uint8_t>(remembered & 0x0F);
      }
      std::uint8_t retriggerTicks = effectCommand == 0x09 ? effectValue : 0;
      std::uint8_t noteCutTicks = 0;
      std::uint8_t noteDelayTicks = 0;
      int fineSlideUp = 0;
      int fineSlideDown = 0;

      if (effectCommand == 0x0E) {
        std::uint8_t subCommand = static_cast<std::uint8_t>((effectValue >> 4) & 0x0F);
        std::uint8_t subValue = static_cast<std::uint8_t>(effectValue & 0x0F);
        switch (subCommand) {
          case 0x1:
            fineSlideUp = static_cast<int>(subValue);
            break;
          case 0x2:
            fineSlideDown = static_cast<int>(subValue);
            break;
          case 0x9:
            retriggerTicks = subValue;
            break;
          case 0xC:
            noteCutTicks = subValue;
            break;
          case 0xD:
            noteDelayTicks = subValue;
            break;
          default:
            break;
        }
      }

      double baseFrequency = midiNoteToFrequencyHz(note);

      if (existingIndex >= 0) {
        if (gate > 0 && (rowNotes[static_cast<std::size_t>(existingIndex)].gateTicks == 0 || gate < rowNotes[static_cast<std::size_t>(existingIndex)].gateTicks)) {
          rowNotes[static_cast<std::size_t>(existingIndex)].gateTicks = gate;
        }
        if (velocity > rowNotes[static_cast<std::size_t>(existingIndex)].velocity) {
          rowNotes[static_cast<std::size_t>(existingIndex)].velocity = velocity;
        }
        rowNotes[static_cast<std::size_t>(existingIndex)].retrigger =
            rowNotes[static_cast<std::size_t>(existingIndex)].retrigger || retrigger;
        if (volumeSlideDelta != 0) {
          rowNotes[static_cast<std::size_t>(existingIndex)].volumeSlideDelta = volumeSlideDelta;
        }
        rowNotes[static_cast<std::size_t>(existingIndex)].arpeggioX = arpeggioX;
        rowNotes[static_cast<std::size_t>(existingIndex)].arpeggioY = arpeggioY;
        rowNotes[static_cast<std::size_t>(existingIndex)].slideUp = slideUp;
        rowNotes[static_cast<std::size_t>(existingIndex)].slideDown = slideDown;
        rowNotes[static_cast<std::size_t>(existingIndex)].tonePortamento = tonePortamento;
        rowNotes[static_cast<std::size_t>(existingIndex)].vibratoSpeed = vibratoSpeed;
        rowNotes[static_cast<std::size_t>(existingIndex)].vibratoDepth = vibratoDepth;
        rowNotes[static_cast<std::size_t>(existingIndex)].retriggerTicks = retriggerTicks;
        rowNotes[static_cast<std::size_t>(existingIndex)].noteCutTicks = noteCutTicks;
        rowNotes[static_cast<std::size_t>(existingIndex)].noteDelayTicks = noteDelayTicks;
        rowNotes[static_cast<std::size_t>(existingIndex)].fineSlideUp = fineSlideUp;
        rowNotes[static_cast<std::size_t>(existingIndex)].fineSlideDown = fineSlideDown;
        rowNotes[static_cast<std::size_t>(existingIndex)].delayedStart = noteDelayTicks > 0;
        rowNotes[static_cast<std::size_t>(existingIndex)].hasStarted = noteDelayTicks == 0;
        rowNotes[static_cast<std::size_t>(existingIndex)].lastRetriggerTick = 0;
        rowNotes[static_cast<std::size_t>(existingIndex)].baseFrequencyHz = baseFrequency;
        rowNotes[static_cast<std::size_t>(existingIndex)].targetFrequencyHz = baseFrequency;
        if (rowNotes[static_cast<std::size_t>(existingIndex)].currentFrequencyHz <= 0.0) {
          rowNotes[static_cast<std::size_t>(existingIndex)].currentFrequencyHz = baseFrequency;
        }
      } else {
        RowNote rowNote;
        rowNote.midiNote = note;
        rowNote.channel = channel;
        rowNote.instrument = key.instrument;
        rowNote.sample = key.sample;
        rowNote.gateTicks = gate;
        rowNote.velocity = velocity;
        rowNote.retrigger = retrigger;
        rowNote.volumeSlideDelta = volumeSlideDelta;
        rowNote.arpeggioX = arpeggioX;
        rowNote.arpeggioY = arpeggioY;
        rowNote.slideUp = slideUp;
        rowNote.slideDown = slideDown;
        rowNote.tonePortamento = tonePortamento;
        rowNote.vibratoSpeed = vibratoSpeed;
        rowNote.vibratoDepth = vibratoDepth;
        rowNote.retriggerTicks = retriggerTicks;
        rowNote.noteCutTicks = noteCutTicks;
        rowNote.noteDelayTicks = noteDelayTicks;
        rowNote.fineSlideUp = fineSlideUp;
        rowNote.fineSlideDown = fineSlideDown;
        rowNote.delayedStart = noteDelayTicks > 0;
        rowNote.hasStarted = noteDelayTicks == 0;
        rowNote.lastRetriggerTick = 0;
        rowNote.baseFrequencyHz = baseFrequency;
        rowNote.currentFrequencyHz = baseFrequency;
        rowNote.targetFrequencyHz = baseFrequency;

        int previousIndex = findRowNoteIndex(activeNotes_, rowNote);
        if (previousIndex >= 0) {
          const RowNote& previousNote = activeNotes_[static_cast<std::size_t>(previousIndex)];
          rowNote.currentFrequencyHz = previousNote.currentFrequencyHz > 0.0
              ? previousNote.currentFrequencyHz
              : baseFrequency;
          rowNote.vibratoPhase = previousNote.vibratoPhase;
        }

        rowNotes.push_back(rowNote);
      }

      if (existingIndex >= 0) {
        auto& existing = rowNotes[static_cast<std::size_t>(existingIndex)];
        if (existing.fineSlideUp > 0) {
          double semitone = static_cast<double>(existing.fineSlideUp) / 8.0;
          existing.currentFrequencyHz *= std::pow(2.0, semitone / 12.0);
        }
        if (existing.fineSlideDown > 0) {
          double semitone = static_cast<double>(existing.fineSlideDown) / 8.0;
          existing.currentFrequencyHz /= std::pow(2.0, semitone / 12.0);
        }
      } else if (!rowNotes.empty()) {
        auto& inserted = rowNotes.back();
        if (inserted.fineSlideUp > 0) {
          double semitone = static_cast<double>(inserted.fineSlideUp) / 8.0;
          inserted.currentFrequencyHz *= std::pow(2.0, semitone / 12.0);
        }
        if (inserted.fineSlideDown > 0) {
          double semitone = static_cast<double>(inserted.fineSlideDown) / 8.0;
          inserted.currentFrequencyHz /= std::pow(2.0, semitone / 12.0);
        }
      }
    }

    for (const RowNote& rowNote : rowNotes) {
      for (RowNote& activeNote : activeNotes_) {
        if (activeNote.channel == rowNote.channel &&
            activeNote.instrument == rowNote.instrument &&
            activeNote.sample == rowNote.sample &&
            activeNote.midiNote != rowNote.midiNote &&
            activeNote.hasStarted && !activeNote.releasedByGate) {
          std::uint8_t targetSlot = (activeNote.sample != 0xFFFF && activeNote.sample <= 255) 
              ? static_cast<std::uint8_t>(activeNote.sample) 
              : activeNote.instrument;
          if (!pluginHost.triggerNoteOffResolved(activeNote.instrument, activeNote.sample, activeNote.midiNote)) {
            audioEngine.noteOff(activeNote.midiNote, targetSlot);
          }
          activeNote.releasedByGate = true;
        }
      }
    }

    for (const RowNote& rowNote : rowNotes) {
      if (rowNote.delayedStart) {
        continue;
      }

      double velocity = static_cast<double>(rowNote.velocity) / 127.0;
      double startFrequency = rowNote.currentFrequencyHz > 0.0 ? rowNote.currentFrequencyHz : midiNoteToFrequencyHz(rowNote.midiNote);
      // Prefer sample slot if specified, otherwise use instrument
      std::uint8_t targetSlot = (rowNote.sample != 0xFFFF && rowNote.sample <= 255) 
          ? static_cast<std::uint8_t>(rowNote.sample) 
          : rowNote.instrument;
      if (!containsNote(activeNotes_, rowNote)) {
        if (!pluginHost.triggerNoteOnResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote, rowNote.velocity, true)) {
          audioEngine.noteOn(rowNote.midiNote, startFrequency, velocity, true, targetSlot);
        }
      } else if (rowNote.retrigger) {
        if (!pluginHost.triggerNoteOnResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote, rowNote.velocity, true)) {
          audioEngine.noteOn(rowNote.midiNote, startFrequency, velocity, true, targetSlot);
        }
      } else {
        if (!pluginHost.triggerNoteOnResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote, rowNote.velocity, false)) {
          audioEngine.noteOn(rowNote.midiNote, startFrequency, velocity, false, targetSlot);
        }
      }
    }

    currentRowNotes_ = rowNotes;
    activeNotes_.clear();
    for (const RowNote& rowNote : currentRowNotes_) {
      if (!rowNote.releasedByGate && rowNote.hasStarted) {
        activeNotes_.push_back(rowNote);
      }
    }

    if (hasPendingJump) {
      transport.jumpToRow(pendingJumpRow);
    }
  }

  if (tickCount != lastObservedTickCount_) {
    for (RowNote& rowNote : currentRowNotes_) {
      if (isChannelMuted(rowNote.channel)) {
        if (rowNote.hasStarted && !rowNote.releasedByGate) {
          std::uint8_t targetSlot = (rowNote.sample != 0xFFFF && rowNote.sample <= 255) 
              ? static_cast<std::uint8_t>(rowNote.sample) 
              : rowNote.instrument;
          if (!pluginHost.triggerNoteOffResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote)) {
            audioEngine.noteOff(rowNote.midiNote, targetSlot);
          }
          rowNote.releasedByGate = true;
        }
        continue;
      }

      if (rowNote.releasedByGate || rowNote.gateTicks == 0) {
        if (rowNote.releasedByGate) {
          continue;
        }
      }

      if (ticksIntoRow == 0) {
        continue;
      }

      if (!rowNote.hasStarted && rowNote.noteDelayTicks > 0 && ticksIntoRow >= rowNote.noteDelayTicks) {
        double velocity = static_cast<double>(rowNote.velocity) / 127.0;
        double startFrequency = rowNote.currentFrequencyHz > 0.0 ? rowNote.currentFrequencyHz : rowNote.baseFrequencyHz;
        std::uint8_t targetSlot = (rowNote.sample != 0xFFFF && rowNote.sample <= 255) 
            ? static_cast<std::uint8_t>(rowNote.sample) 
            : rowNote.instrument;
        if (!pluginHost.triggerNoteOnResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote, rowNote.velocity, true)) {
          audioEngine.noteOn(rowNote.midiNote, startFrequency, velocity, true, targetSlot);
        }
        rowNote.hasStarted = true;
        rowNote.delayedStart = false;
      }

      if (!rowNote.hasStarted) {
        continue;
      }

      if (rowNote.gateTicks > 0 && ticksIntoRow >= rowNote.gateTicks) {
        std::uint8_t targetSlot = (rowNote.sample != 0xFFFF && rowNote.sample <= 255)
            ? static_cast<std::uint8_t>(rowNote.sample)
            : rowNote.instrument;
        if (!pluginHost.triggerNoteOffResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote)) {
          audioEngine.noteOff(rowNote.midiNote, targetSlot);
        }
        rowNote.releasedByGate = true;
        continue;
      }

      if (rowNote.noteCutTicks > 0 && ticksIntoRow >= rowNote.noteCutTicks) {
        std::uint8_t targetSlot = (rowNote.sample != 0xFFFF && rowNote.sample <= 255) 
            ? static_cast<std::uint8_t>(rowNote.sample) 
            : rowNote.instrument;
        if (!pluginHost.triggerNoteOffResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote)) {
          audioEngine.noteOff(rowNote.midiNote, targetSlot);
        }
        rowNote.releasedByGate = true;
        continue;
      }

      if (rowNote.retriggerTicks > 0 && ticksIntoRow > 0 && ticksIntoRow % rowNote.retriggerTicks == 0 && rowNote.lastRetriggerTick != ticksIntoRow) {
        double velocity = static_cast<double>(rowNote.velocity) / 127.0;
        double frequency = rowNote.currentFrequencyHz > 0.0 ? rowNote.currentFrequencyHz : rowNote.baseFrequencyHz;
        std::uint8_t targetSlot = (rowNote.sample != 0xFFFF && rowNote.sample <= 255) 
            ? static_cast<std::uint8_t>(rowNote.sample) 
            : rowNote.instrument;
        if (!pluginHost.triggerNoteOnResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote, rowNote.velocity, true)) {
          audioEngine.noteOn(rowNote.midiNote, frequency, velocity, true, targetSlot);
        }
        rowNote.lastRetriggerTick = ticksIntoRow;
      }

      if (rowNote.volumeSlideDelta != 0) {
        int updatedVelocity = std::clamp<int>(
            static_cast<int>(rowNote.velocity) + rowNote.volumeSlideDelta,
            1,
            127);
        if (updatedVelocity != rowNote.velocity) {
          rowNote.velocity = static_cast<std::uint8_t>(updatedVelocity);
          double velocity = static_cast<double>(rowNote.velocity) / 127.0;
          std::uint8_t targetSlot = (rowNote.sample != 0xFFFF && rowNote.sample <= 255) 
              ? static_cast<std::uint8_t>(rowNote.sample) 
              : rowNote.instrument;
          if (!pluginHost.triggerNoteOnResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote, rowNote.velocity, false)) {
            audioEngine.noteOn(rowNote.midiNote, midiNoteToFrequencyHz(rowNote.midiNote), velocity, false, targetSlot);
          }
        }
      }

      if (rowNote.slideUp > 0) {
        double semitone = static_cast<double>(rowNote.slideUp) / 16.0;
        rowNote.currentFrequencyHz *= std::pow(2.0, semitone / 12.0);
      }

      if (rowNote.slideDown > 0) {
        double semitone = static_cast<double>(rowNote.slideDown) / 16.0;
        rowNote.currentFrequencyHz /= std::pow(2.0, semitone / 12.0);
      }

      if (rowNote.tonePortamento > 0) {
        double stepSemitone = static_cast<double>(rowNote.tonePortamento) / 32.0;
        double stepRatio = std::pow(2.0, stepSemitone / 12.0);
        if (rowNote.currentFrequencyHz < rowNote.targetFrequencyHz) {
          rowNote.currentFrequencyHz = std::min(rowNote.targetFrequencyHz, rowNote.currentFrequencyHz * stepRatio);
        } else if (rowNote.currentFrequencyHz > rowNote.targetFrequencyHz) {
          rowNote.currentFrequencyHz = std::max(rowNote.targetFrequencyHz, rowNote.currentFrequencyHz / stepRatio);
        }
      }

      double modulationFrequency = rowNote.currentFrequencyHz > 0.0
          ? rowNote.currentFrequencyHz
          : rowNote.baseFrequencyHz;

      if (rowNote.arpeggioX > 0 || rowNote.arpeggioY > 0) {
        std::uint32_t cycle = ticksIntoRow % 3;
        int offset = 0;
        if (cycle == 1) {
          offset = rowNote.arpeggioX;
        } else if (cycle == 2) {
          offset = rowNote.arpeggioY;
        }
        modulationFrequency = rowNote.baseFrequencyHz * std::pow(2.0, static_cast<double>(offset) / 12.0);
      }

      if (rowNote.vibratoSpeed > 0 && rowNote.vibratoDepth > 0) {
        rowNote.vibratoPhase += static_cast<double>(rowNote.vibratoSpeed) * 0.25;
        double vibratoScale = 1.0 + (std::sin(rowNote.vibratoPhase) * static_cast<double>(rowNote.vibratoDepth) * 0.004);
        modulationFrequency *= std::max(0.2, vibratoScale);
      }

      if (modulationFrequency > 0.0) {
        double velocity = static_cast<double>(rowNote.velocity) / 127.0;
        if (!pluginHost.triggerNoteOnResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote, rowNote.velocity, false)) {
          std::uint8_t targetSlot = (rowNote.sample != 0xFFFF && rowNote.sample <= 255)
              ? static_cast<std::uint8_t>(rowNote.sample)
              : rowNote.instrument;
          audioEngine.noteOn(rowNote.midiNote, modulationFrequency, velocity, false, targetSlot);
        }
      }


    }

    activeNotes_.clear();
    for (const RowNote& rowNote : currentRowNotes_) {
      if (!rowNote.releasedByGate && rowNote.hasStarted) {
        activeNotes_.push_back(rowNote);
      }
    }
  }

  lastObservedTickCount_ = tickCount;
}

std::uint64_t Sequencer::dispatchCount() const {
  return dispatchCount_;
}

int Sequencer::activeMidiNote() const {
  if (activeNotes_.empty()) {
    return -1;
  }
  return activeNotes_.front().midiNote;
}

std::size_t Sequencer::activeVoiceCount() const {
  return activeNotes_.size();
}

int Sequencer::activeMidiNoteAt(std::size_t index) const {
  if (index >= activeNotes_.size()) {
    return -1;
  }
  return activeNotes_[index].midiNote;
}

bool Sequencer::sameKey(const RowNote& a, const RowNote& b) {
  return a.midiNote == b.midiNote &&
         a.channel == b.channel &&
         a.instrument == b.instrument &&
         a.sample == b.sample;
}

bool Sequencer::containsNote(const std::vector<RowNote>& notes, const RowNote& note) {
  return findRowNoteIndex(notes, note) >= 0;
}

int Sequencer::findRowNoteIndex(const std::vector<RowNote>& notes, const RowNote& note) {
  for (std::size_t i = 0; i < notes.size(); ++i) {
    if (sameKey(notes[i], note)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

double Sequencer::midiNoteToFrequencyHz(int midiNote) {
  int clamped = std::clamp(midiNote, 0, 127);
  return 440.0 * std::pow(2.0, static_cast<double>(clamped - 69) / 12.0);
}

}  // namespace extracker
