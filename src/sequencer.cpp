#include "extracker/sequencer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "extracker/audio_engine.hpp"
#include "extracker/pattern_editor.hpp"
#include "extracker/plugin_host.hpp"
#include "extracker/transport.hpp"

namespace extracker {

namespace {

constexpr std::uint32_t kInvalidLoopRow = std::numeric_limits<std::uint32_t>::max();

double quantizeToNearestSemitone(double frequencyHz) {
  if (frequencyHz <= 0.0) {
    return frequencyHz;
  }
  double midi = 69.0 + 12.0 * std::log2(frequencyHz / 440.0);
  double nearestMidi = std::round(midi);
  return 440.0 * std::pow(2.0, (nearestMidi - 69.0) / 12.0);
}

double waveformSample(std::uint8_t waveform, double phase) {
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kTwoPi = 2.0 * kPi;

  double wrapped = std::fmod(phase, kTwoPi);
  if (wrapped < 0.0) {
    wrapped += kTwoPi;
  }
  double normalized = wrapped / kTwoPi;

  switch (waveform & 0x03) {
    case 0x1:
      // Rising saw in [-1, 1].
      return (2.0 * normalized) - 1.0;
    case 0x2:
      return wrapped < kPi ? 1.0 : -1.0;
    case 0x3:
      // Triangle in [-1, 1].
      return 1.0 - (4.0 * std::abs(normalized - 0.5));
    case 0x0:
    default:
      return std::sin(phase);
  }
}

}  // namespace

Sequencer::Sequencer()
    : lastObservedRow_(0),
      lastObservedTickCount_(0),
      hasObservedRow_(false),
      dispatchCount_(0),
  activeNotes_{},
  rowDelayTargetRow_(kInvalidLoopRow),
  rowDelayRowsRemaining_(0),
  patternWrappedSinceLastQuery_(false),
  suppressNextPatternWrapDetection_(false) {}

void Sequencer::reset() {
  lastObservedRow_ = 0;
  lastObservedTickCount_ = 0;
  hasObservedRow_ = false;
  dispatchCount_ = 0;
  activeNotes_.clear();
  currentRowNotes_.clear();
  effectMemoryByChannel_.clear();
  lastContinuousEffectCommandByChannel_.clear();
  panByChannel_.clear();
  legacyFilterEnabledByChannel_.clear();
  funkRepeatTicksByChannel_.clear();
  rowDelayTargetRow_ = kInvalidLoopRow;
  rowDelayRowsRemaining_ = 0;
  loopStartRowByChannel_.clear();
  loopRemainingByChannel_.clear();
  loopEndRowByChannel_.clear();
  glissandoEnabledByChannel_.clear();
  vibratoWaveformByChannel_.clear();
  tremoloWaveformByChannel_.clear();
  patternWrappedSinceLastQuery_ = false;
  suppressNextPatternWrapDetection_ = false;
}

void Sequencer::update(
  const PatternEditor& pattern,
  Transport& transport,
  AudioEngine& audioEngine,
  PluginHost& pluginHost,
  const std::vector<bool>* mutedChannels) {
  patternWrappedSinceLastQuery_ = false;

  auto isChannelMuted = [mutedChannels](std::size_t channel) {
    return mutedChannels != nullptr && channel < mutedChannels->size() && (*mutedChannels)[channel];
  };

  std::uint32_t row = transport.currentRow();
  std::uint64_t tickCount = transport.tickCount();
  std::uint32_t ticksPerRow = std::max<std::uint32_t>(transport.ticksPerRow(), 1);
  std::uint32_t ticksIntoRow = static_cast<std::uint32_t>(tickCount % ticksPerRow);

  bool rowChanged = !hasObservedRow_ || row != lastObservedRow_;
  if (rowChanged) {
    if (suppressNextPatternWrapDetection_) {
      suppressNextPatternWrapDetection_ = false;
    } else if (hasObservedRow_ &&
               row == 0 &&
               pattern.rows() > 0 &&
               lastObservedRow_ == pattern.rows() - 1) {
      // A natural row wrap marks the end of the currently playing pattern.
      patternWrappedSinceLastQuery_ = true;
    }

    if (rowDelayRowsRemaining_ > 0 && rowDelayTargetRow_ != kInvalidLoopRow && row != rowDelayTargetRow_) {
      transport.jumpToRow(rowDelayTargetRow_);
      rowDelayRowsRemaining_ = static_cast<std::uint8_t>(rowDelayRowsRemaining_ - 1);
      hasObservedRow_ = false;
      return;
    }
    if (rowDelayRowsRemaining_ == 0 && row != rowDelayTargetRow_) {
      rowDelayTargetRow_ = kInvalidLoopRow;
    }

    std::uint32_t pendingJumpRow = row;
    bool hasPendingJump = false;

    hasObservedRow_ = true;
    lastObservedRow_ = row;
    dispatchCount_ += 1;

    if (effectMemoryByChannel_.size() != pattern.channels()) {
      effectMemoryByChannel_.assign(pattern.channels(), std::array<std::uint8_t, 16>{});
    }
    if (lastContinuousEffectCommandByChannel_.size() != pattern.channels()) {
      lastContinuousEffectCommandByChannel_.assign(pattern.channels(), 0xFF);
    }
    if (panByChannel_.size() != pattern.channels()) {
      panByChannel_.assign(pattern.channels(), 0x80);
    }
    if (legacyFilterEnabledByChannel_.size() != pattern.channels()) {
      legacyFilterEnabledByChannel_.assign(pattern.channels(), false);
    }
    if (funkRepeatTicksByChannel_.size() != pattern.channels()) {
      funkRepeatTicksByChannel_.assign(pattern.channels(), 0);
    }
    if (loopStartRowByChannel_.size() != pattern.channels()) {
      loopStartRowByChannel_.assign(pattern.channels(), 0);
    }
    if (loopRemainingByChannel_.size() != pattern.channels()) {
      loopRemainingByChannel_.assign(pattern.channels(), 0);
    }
    if (loopEndRowByChannel_.size() != pattern.channels()) {
      loopEndRowByChannel_.assign(pattern.channels(), kInvalidLoopRow);
    }
    if (glissandoEnabledByChannel_.size() != pattern.channels()) {
      glissandoEnabledByChannel_.assign(pattern.channels(), false);
    }
    if (vibratoWaveformByChannel_.size() != pattern.channels()) {
      vibratoWaveformByChannel_.assign(pattern.channels(), 0);
    }
    if (tremoloWaveformByChannel_.size() != pattern.channels()) {
      tremoloWaveformByChannel_.assign(pattern.channels(), 0);
    }

    std::vector<RowNote> rowNotes;
    for (std::size_t channel = 0; channel < pattern.channels(); ++channel) {
      if (isChannelMuted(channel)) {
        continue;
      }

      std::uint8_t effectCommand = pattern.effectCommandAt(static_cast<int>(row), static_cast<int>(channel));
      std::uint8_t effectValue = pattern.effectValueAt(static_cast<int>(row), static_cast<int>(channel));

      // Auto-carry: if the cell is blank (both raw values 0), restore the last
      // continuous effect command so it keeps running until explicitly cleared.
      // One-shot effects (0B jump, 0C volume-set, 0D break, 0E extended,
      // 0F speed/tempo, 17 set-TPB) are never auto-carried.
      static constexpr std::uint8_t kNoContinuous = 0xFF;
      static auto isContinuous = [](std::uint8_t cmd) -> bool {
        return cmd == 0x00 || cmd == 0x01 || cmd == 0x02 || cmd == 0x03 ||
               cmd == 0x04 || cmd == 0x05 || cmd == 0x06 || cmd == 0x07 || cmd == 0x08 || cmd == 0x09 ||
               cmd == 0x0A;
      };
      if (effectCommand == 0 && effectValue == 0) {
        std::uint8_t last = lastContinuousEffectCommandByChannel_[channel];
        if (last != kNoContinuous && isContinuous(last)) {
          effectCommand = last;
          effectValue = effectMemoryByChannel_[channel][effectCommand];
        }
      } else if (isContinuous(effectCommand) && !(effectCommand == 0 && effectValue == 0)) {
        lastContinuousEffectCommandByChannel_[channel] = effectCommand;
      } else {
        // One-shot effect: break the carry chain.
        lastContinuousEffectCommandByChannel_[channel] = kNoContinuous;
      }

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
        case 0x17:
          if (effectValue > 0) {
            transport.setTicksPerBeat(static_cast<std::uint32_t>(effectValue));
          }
          break;
        default:
          break;
      }

      // Apply channel control subcommands on FX-only cells as well.
      if (effectCommand == 0x0E) {
        std::uint8_t subCommand = static_cast<std::uint8_t>((effectValue >> 4) & 0x0F);
        std::uint8_t subValue = static_cast<std::uint8_t>(effectValue & 0x0F);
        if (subCommand == 0x3) {
          glissandoEnabledByChannel_[channel] = subValue != 0;
        } else if (subCommand == 0x0) {
          legacyFilterEnabledByChannel_[channel] = subValue != 0;
        } else if (subCommand == 0x4) {
          vibratoWaveformByChannel_[channel] = static_cast<std::uint8_t>(subValue & 0x03);
        } else if (subCommand == 0x7) {
          tremoloWaveformByChannel_[channel] = static_cast<std::uint8_t>(subValue & 0x03);
        } else if (subCommand == 0x8) {
          panByChannel_[channel] = static_cast<std::uint8_t>(subValue * 17);
        } else if (subCommand == 0x6) {
          if (subValue == 0) {
            loopStartRowByChannel_[channel] = row;
          } else {
            std::uint32_t loopStart = loopStartRowByChannel_[channel];
            if (loopStart >= pattern.rows()) {
              loopStart = 0;
              loopStartRowByChannel_[channel] = 0;
            }
            std::uint8_t& remaining = loopRemainingByChannel_[channel];
            std::uint32_t& loopEnd = loopEndRowByChannel_[channel];
            if (remaining == 0) {
              if (loopEnd == row) {
                // Loop for this end row has just completed; allow progression.
                loopEnd = kInvalidLoopRow;
              } else {
                remaining = subValue;
                loopEnd = row;
              }
            }
            if (remaining > 0) {
              hasPendingJump = true;
              pendingJumpRow = loopStart;
              remaining = static_cast<std::uint8_t>(remaining - 1);
            }
          }
        } else if (subCommand == 0xE) {
          if (subValue > 0) {
            rowDelayTargetRow_ = row;
            rowDelayRowsRemaining_ = subValue;
          }
        } else if (subCommand == 0xF) {
          funkRepeatTicksByChannel_[channel] = subValue;
        }
      } else if (effectCommand == 0x08) {
        panByChannel_[channel] = effectValue;
      }

      int note = pattern.noteAt(static_cast<int>(row), static_cast<int>(channel));
      if (note < 0) {
        // Cxx (volume set) on an empty cell: carry the active note for this channel
        // forward with the new velocity so chord fades work without retriggering.
        if (effectCommand == 0x0C && effectValue > 0) {
          for (const RowNote& activeNote : activeNotes_) {
            if (activeNote.channel == channel && !activeNote.releasedByGate) {
              RowNote carried = activeNote;
              carried.velocity = std::clamp<std::uint8_t>(effectValue, 1, 127);
              carried.gateTicks = 0;
              carried.retrigger = false;
              carried.volumeSlideDelta = 0;
              carried.retriggerTicks = 0;
              carried.noteCutTicks = 0;
              carried.noteDelayTicks = 0;
              carried.delayedStart = false;
              carried.hasStarted = true;
              carried.arpeggioX = 0;
              carried.arpeggioY = 0;
              carried.slideUp = 0;
              carried.slideDown = 0;
              carried.tonePortamento = 0;
              carried.vibratoSpeed = 0;
              carried.vibratoDepth = 0;
              carried.tremoloSpeed = 0;
              carried.tremoloDepth = 0;
              carried.legacyFilterEnabled = legacyFilterEnabledByChannel_[channel];
              carried.fineSlideUp = 0;
              carried.fineSlideDown = 0;
              carried.fineTuneSemitone = 0;
              carried.pan = panByChannel_[channel];
              carried.glissandoEnabled = glissandoEnabledByChannel_[channel];
              carried.vibratoWaveform = vibratoWaveformByChannel_[channel];
              carried.tremoloWaveform = tremoloWaveformByChannel_[channel];
              rowNotes.push_back(carried);
              break;
            }
          }
        }
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
      std::uint8_t tremoloSpeed = 0;
      std::uint8_t tremoloDepth = 0;
      if (effectCommand == 0x04) {
        vibratoSpeed = static_cast<std::uint8_t>((effectValue >> 4) & 0x0F);
        vibratoDepth = static_cast<std::uint8_t>(effectValue & 0x0F);
      } else if (effectCommand == 0x06) {
        std::uint8_t remembered = effectMemoryByChannel_[channel][0x04];
        vibratoSpeed = static_cast<std::uint8_t>((remembered >> 4) & 0x0F);
        vibratoDepth = static_cast<std::uint8_t>(remembered & 0x0F);
      }
      if (effectCommand == 0x07) {
        tremoloSpeed = static_cast<std::uint8_t>((effectValue >> 4) & 0x0F);
        tremoloDepth = static_cast<std::uint8_t>(effectValue & 0x0F);
      }
      std::uint8_t retriggerTicks = effectCommand == 0x09 ? effectValue : 0;
      std::uint8_t noteCutTicks = 0;
      std::uint8_t noteDelayTicks = 0;
      int fineSlideUp = 0;
      int fineSlideDown = 0;
      int fineVolumeUp = 0;
      int fineVolumeDown = 0;
      int fineTuneSemitone = 0;
      std::uint8_t pan = panByChannel_[channel];
      bool legacyFilterEnabled = legacyFilterEnabledByChannel_[channel];
      bool glissandoEnabled = glissandoEnabledByChannel_[channel];
      std::uint8_t vibratoWaveform = vibratoWaveformByChannel_[channel];
      std::uint8_t tremoloWaveform = tremoloWaveformByChannel_[channel];

      if (effectCommand == 0x08) {
        pan = effectValue;
        panByChannel_[channel] = pan;
      }

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
          case 0xA:
            fineVolumeUp = static_cast<int>(subValue);
            break;
          case 0xB:
            fineVolumeDown = static_cast<int>(subValue);
            break;
          case 0x5:
            fineTuneSemitone = static_cast<int>(subValue);
            if (fineTuneSemitone > 7) {
              fineTuneSemitone -= 16;
            }
            break;
          case 0x3:
            glissandoEnabled = subValue != 0;
            glissandoEnabledByChannel_[channel] = glissandoEnabled;
            break;
          case 0x0:
            legacyFilterEnabled = subValue != 0;
            legacyFilterEnabledByChannel_[channel] = legacyFilterEnabled;
            break;
          case 0x4:
            vibratoWaveform = static_cast<std::uint8_t>(subValue & 0x03);
            vibratoWaveformByChannel_[channel] = vibratoWaveform;
            break;
          case 0x7:
            tremoloWaveform = static_cast<std::uint8_t>(subValue & 0x03);
            tremoloWaveformByChannel_[channel] = tremoloWaveform;
            break;
          case 0x8:
            pan = static_cast<std::uint8_t>(subValue * 17);
            panByChannel_[channel] = pan;
            break;
          case 0xC:
            noteCutTicks = subValue;
            break;
          case 0xD:
            noteDelayTicks = subValue;
            break;
          case 0xF:
            retriggerTicks = subValue;
            funkRepeatTicksByChannel_[channel] = subValue;
            break;
          default:
            break;
        }
      }

      if (retriggerTicks == 0 && funkRepeatTicksByChannel_[channel] > 0) {
        retriggerTicks = funkRepeatTicksByChannel_[channel];
      }

      if (fineVolumeUp > 0 || fineVolumeDown > 0) {
        int adjustedVelocity = std::clamp<int>(
            static_cast<int>(velocity) + fineVolumeUp - fineVolumeDown,
            1,
            127);
        velocity = static_cast<std::uint8_t>(adjustedVelocity);
      }

      double baseFrequency = midiNoteToFrequencyHz(note);
      if (fineTuneSemitone != 0) {
        baseFrequency *= std::pow(2.0, static_cast<double>(fineTuneSemitone) / 12.0);
      }

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
        rowNotes[static_cast<std::size_t>(existingIndex)].tremoloSpeed = tremoloSpeed;
        rowNotes[static_cast<std::size_t>(existingIndex)].tremoloDepth = tremoloDepth;
        rowNotes[static_cast<std::size_t>(existingIndex)].legacyFilterEnabled = legacyFilterEnabled;
        rowNotes[static_cast<std::size_t>(existingIndex)].retriggerTicks = retriggerTicks;
        rowNotes[static_cast<std::size_t>(existingIndex)].noteCutTicks = noteCutTicks;
        rowNotes[static_cast<std::size_t>(existingIndex)].noteDelayTicks = noteDelayTicks;
        rowNotes[static_cast<std::size_t>(existingIndex)].fineSlideUp = fineSlideUp;
        rowNotes[static_cast<std::size_t>(existingIndex)].fineSlideDown = fineSlideDown;
        rowNotes[static_cast<std::size_t>(existingIndex)].fineTuneSemitone = fineTuneSemitone;
        rowNotes[static_cast<std::size_t>(existingIndex)].pan = pan;
        rowNotes[static_cast<std::size_t>(existingIndex)].glissandoEnabled = glissandoEnabled;
        rowNotes[static_cast<std::size_t>(existingIndex)].vibratoWaveform = vibratoWaveform;
        rowNotes[static_cast<std::size_t>(existingIndex)].tremoloWaveform = tremoloWaveform;
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
        rowNote.tremoloSpeed = tremoloSpeed;
        rowNote.tremoloDepth = tremoloDepth;
        rowNote.legacyFilterEnabled = legacyFilterEnabled;
        rowNote.retriggerTicks = retriggerTicks;
        rowNote.noteCutTicks = noteCutTicks;
        rowNote.noteDelayTicks = noteDelayTicks;
        rowNote.fineSlideUp = fineSlideUp;
        rowNote.fineSlideDown = fineSlideDown;
        rowNote.fineTuneSemitone = fineTuneSemitone;
        rowNote.pan = pan;
        rowNote.glissandoEnabled = glissandoEnabled;
        rowNote.vibratoWaveform = vibratoWaveform;
        rowNote.tremoloWaveform = tremoloWaveform;
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
          rowNote.tremoloPhase = previousNote.tremoloPhase;
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

      std::uint8_t startVelocity = rowNote.velocity;
      if (rowNote.legacyFilterEnabled) {
        startVelocity = static_cast<std::uint8_t>(std::clamp<int>(
            static_cast<int>(std::lround(static_cast<double>(rowNote.velocity) * 0.75)),
            1,
            127));
      }
      double velocity = static_cast<double>(startVelocity) / 127.0;
      double startFrequency = rowNote.currentFrequencyHz > 0.0 ? rowNote.currentFrequencyHz : midiNoteToFrequencyHz(rowNote.midiNote);
      // Prefer sample slot if specified, otherwise use instrument
      std::uint8_t targetSlot = (rowNote.sample != 0xFFFF && rowNote.sample <= 255) 
          ? static_cast<std::uint8_t>(rowNote.sample) 
          : rowNote.instrument;
      if (!containsNote(activeNotes_, rowNote)) {
        if (!pluginHost.triggerNoteOnResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote, startVelocity, true)) {
          audioEngine.noteOn(rowNote.midiNote, startFrequency, velocity, true, targetSlot, static_cast<double>(rowNote.pan) / 255.0);
        }
      } else if (rowNote.retrigger) {
        if (!pluginHost.triggerNoteOnResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote, startVelocity, true)) {
          audioEngine.noteOn(rowNote.midiNote, startFrequency, velocity, true, targetSlot, static_cast<double>(rowNote.pan) / 255.0);
        }
      } else {
        if (!pluginHost.triggerNoteOnResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote, startVelocity, false)) {
          audioEngine.noteOn(rowNote.midiNote, startFrequency, velocity, false, targetSlot, static_cast<double>(rowNote.pan) / 255.0);
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
      suppressNextPatternWrapDetection_ = true;
      transport.jumpToRow(pendingJumpRow);
      // Force next update to dispatch the jumped-to row.
      hasObservedRow_ = false;
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
        std::uint8_t startVelocity = rowNote.velocity;
        if (rowNote.legacyFilterEnabled) {
          startVelocity = static_cast<std::uint8_t>(std::clamp<int>(
              static_cast<int>(std::lround(static_cast<double>(rowNote.velocity) * 0.75)),
              1,
              127));
        }
        double velocity = static_cast<double>(startVelocity) / 127.0;
        double startFrequency = rowNote.currentFrequencyHz > 0.0 ? rowNote.currentFrequencyHz : rowNote.baseFrequencyHz;
        std::uint8_t targetSlot = (rowNote.sample != 0xFFFF && rowNote.sample <= 255) 
            ? static_cast<std::uint8_t>(rowNote.sample) 
            : rowNote.instrument;
        if (!pluginHost.triggerNoteOnResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote, startVelocity, true)) {
          audioEngine.noteOn(rowNote.midiNote, startFrequency, velocity, true, targetSlot, static_cast<double>(rowNote.pan) / 255.0);
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
        std::uint8_t retriggerVelocity = rowNote.velocity;
        if (rowNote.legacyFilterEnabled) {
          retriggerVelocity = static_cast<std::uint8_t>(std::clamp<int>(
              static_cast<int>(std::lround(static_cast<double>(rowNote.velocity) * 0.75)),
              1,
              127));
        }
        double velocity = static_cast<double>(retriggerVelocity) / 127.0;
        double frequency = rowNote.currentFrequencyHz > 0.0 ? rowNote.currentFrequencyHz : rowNote.baseFrequencyHz;
        std::uint8_t targetSlot = (rowNote.sample != 0xFFFF && rowNote.sample <= 255) 
            ? static_cast<std::uint8_t>(rowNote.sample) 
            : rowNote.instrument;
        if (!pluginHost.triggerNoteOnResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote, retriggerVelocity, true)) {
          audioEngine.noteOn(rowNote.midiNote, frequency, velocity, true, targetSlot, static_cast<double>(rowNote.pan) / 255.0);
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
            audioEngine.noteOn(rowNote.midiNote, midiNoteToFrequencyHz(rowNote.midiNote), velocity, false, targetSlot, static_cast<double>(rowNote.pan) / 255.0);
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
        if (rowNote.glissandoEnabled) {
          rowNote.currentFrequencyHz = quantizeToNearestSemitone(rowNote.currentFrequencyHz);
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
        double lfo = waveformSample(rowNote.vibratoWaveform, rowNote.vibratoPhase);
        double vibratoScale = 1.0 + (lfo * static_cast<double>(rowNote.vibratoDepth) * 0.004);
        modulationFrequency *= std::max(0.2, vibratoScale);
      }

      if (modulationFrequency > 0.0) {
        std::uint8_t outputVelocity = rowNote.velocity;
        if (rowNote.legacyFilterEnabled) {
          outputVelocity = static_cast<std::uint8_t>(std::clamp<int>(
              static_cast<int>(std::lround(static_cast<double>(outputVelocity) * 0.75)),
              1,
              127));
        }
        if (rowNote.tremoloSpeed > 0 && rowNote.tremoloDepth > 0) {
          rowNote.tremoloPhase += static_cast<double>(rowNote.tremoloSpeed) * 0.25;
          double lfo = waveformSample(rowNote.tremoloWaveform, rowNote.tremoloPhase);
          double tremoloAmount = lfo * static_cast<double>(rowNote.tremoloDepth) * 0.06;
          int scaledVelocity = static_cast<int>(std::lround(static_cast<double>(rowNote.velocity) * (1.0 + tremoloAmount)));
          outputVelocity = static_cast<std::uint8_t>(std::clamp(scaledVelocity, 1, 127));
        }
        double velocity = static_cast<double>(outputVelocity) / 127.0;
        if (!pluginHost.triggerNoteOnResolved(rowNote.instrument, rowNote.sample, rowNote.midiNote, outputVelocity, false)) {
          std::uint8_t targetSlot = (rowNote.sample != 0xFFFF && rowNote.sample <= 255)
              ? static_cast<std::uint8_t>(rowNote.sample)
              : rowNote.instrument;
          audioEngine.noteOn(rowNote.midiNote, modulationFrequency, velocity, false, targetSlot, static_cast<double>(rowNote.pan) / 255.0);
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

bool Sequencer::consumePatternWrapEvent() {
  bool wrapped = patternWrappedSinceLastQuery_;
  patternWrappedSinceLastQuery_ = false;
  return wrapped;
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

std::uint8_t Sequencer::panByChannel(std::size_t channel) const {
  if (channel >= panByChannel_.size()) {
    return 0x80;
  }
  return panByChannel_[channel];
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
