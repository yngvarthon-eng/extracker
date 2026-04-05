# exTracker

exTracker is a Linux-first music tracker prototype focused on pattern-based composition with a real-time audio engine.

## Current status

This repository contains a clean C++17 + CMake foundation with module placeholders for:

- Audio engine
- Pattern editor
- Plugin host

Audio output milestone 1 is in progress with a backend abstraction:

- ALSA playback backend (when libasound development files are available)
- Null backend fallback for environments without ALSA or an active audio device

Pattern milestone 2 is now in place with an in-memory grid model:

- fixed rows x channels layout (default 64x8)
- note step insert/read/clear operations
- MIDI range validation and out-of-bounds safety
- per-step instrument, gate, velocity, retrigger, and effect command/value columns

Sequencer effect runtime support is now in place for multiple commands:

- `0x00`: arpeggio (base + two semitone offsets)
- `0x01`: pitch slide up per tick
- `0x02`: pitch slide down per tick
- `0x03`: tone portamento toward target pitch
- `0x04`: vibrato (speed/depth)
- `0x05`: tone portamento + volume slide
- `0x06`: vibrato + volume slide
- `0x09`: retrigger note every N ticks
- `0x0C`: set note velocity from effect value
- `0x0A`: volume slide per tick (high nibble up, low nibble down)
- `0x0F`: set speed (`value < 32`) or tempo BPM (`value >= 32`)
- `0x0B`: pattern jump to row
- `0x0D`: pattern break to row
- `0x0E1`: fine slide up
- `0x0E2`: fine slide down
- `0x0E9`: retrigger note every N ticks
- `0x0EC`: note cut at tick N
- `0x0ED`: note delay by N ticks
- Effect memory is enabled for reused command values when effect value is zero
- Per-tick effects apply from tick 1 (tick-0 row setup is handled separately)

Transport milestone 3 is now in place:

- tempo and ticks-per-beat configuration
- play/stop lifecycle and background clock thread
- running tick counter for sequencer timing
- row advancement via ticks-per-row with pattern wraparound

Sequencer bridge milestone is now in place:

- dispatch loop reads current transport row
- row note lookup across pattern channels
- MIDI note to Hz mapping into audio test tone frequency
- polyphonic row dispatch across channels
- note-off on empty rows (audio voice set cleared)

Voice lifecycle milestone is now in place:

- sequencer emits note-on/note-off deltas on row changes
- audio engine keeps persistent active voices by MIDI note
- basic attack/release envelope behavior for running audio backend
- per-step gate ticks can release notes before row end
- per-step velocity scales voice level
- per-step retrigger restarts phase/envelope on repeated notes
- per-step instrument IDs route notes into separate voice groups

Plugin host routing milestone is now in place:

- instrument IDs can be assigned to plugin slots
- plugin host exposes route lookup per instrument
- sequencer note events trigger plugin note-on/note-off callbacks
- audio engine renders plugin-owned voice state for assigned instruments
- audio engine can now pull rendered samples directly from plugin host callbacks

Formal plugin process interface is now in place:

- instrument slots own concrete plugin instances
- plugins receive noteOn/noteOff callbacks
- plugins render into audio buffers via host render callback
- plugin parameters can be set/read per instrument slot
- plugin registry/discovery layer resolves plugin IDs through factories

CLI command architecture is now split into focused modules:

- `main.cpp` handles bootstrap/composition and command registration
- `core_cli` handles transport/status/reset/save/load
- `midi_cli` handles MIDI commands and live event handling
- `note_cli` handles note editing commands
- `pattern_cli` handles pattern print/play/template
- `record_cli` handles recording workflow commands
- `plugin_cli` handles help/plugin/sine commands

External plugin adapter scaffold is now in place:

- plugin host supports registering external adapter backends
- adapter backends can discover plugins and register factories
- default build uses a disabled adapter placeholder for safe startup on all systems

LV2 backend milestone is now in place:

- plugin host includes an LV2 manifest adapter backend
- adapter scans LV2 bundles and registers discovered plugin IDs into the factory registry
- discovered LV2 plugins now probe shared libraries through `lv2_descriptor`
- audio path still uses fallback instrument synthesis until full LV2 port/audio bridge is completed

## Build (Linux)

### Prerequisites

- CMake 3.16+
- Ninja
- GCC or Clang

### Configure

```bash
cmake -S . -B build -G Ninja
```

If Ninja is unavailable, use Makefiles:

```bash
cmake -S . -B build-make -G "Unix Makefiles"
```

### Build

```bash
cmake --build build
```

Makefiles variant:

```bash
cmake --build build-make
```

### Run

```bash
./build/extracker
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

## CLI Record Cursor Quick Reference

- `record cursor status` shows the current record cursor row.
- `record cursor <row>` sets an absolute row.
- `record cursor +N` and `record cursor -N` move the cursor by a relative amount.
- `record cursor start` and `record cursor end` jump to first/last row.
- `record cursor next` and `record cursor prev` move by the current `record jump` amount.

## CLI Record Note Quick Reference

- `record note <midi>` uses the current `midi instrument` as the default instrument.
- `record note <midi> [instr] [vel] [fx] [fxval]` sets explicit values.
- `record note <midi> vel <vel> [fx] [fxval]` keeps default instrument while setting velocity/effect.
- `record note <midi> fx <fx> <fxval>` keeps default instrument and default velocity while setting effect.
- `record note <midi> instr <i> [vel <v>] [fx <f> <fv>]` supports keyword arguments in any order.
- `record note dry <midi> ...` validates/parses and previews the target write without modifying the pattern.

## CLI Note Set Quick Reference

- `note set <row> <ch> <midi> <instr> [vel] [fx] [fxval]` writes directly to the pattern.
- `note set dry <row> <ch> <midi> <instr> [vel] [fx] [fxval]` previews parsed values without writing.
- `note clear dry <row> <ch>` previews a clear operation without modifying the pattern.
- `note vel dry <row> <ch> <vel>` previews a velocity edit without writing.
- `note gate dry <row> <ch> <ticks>` previews a gate edit without writing.
- `note fx dry <row> <ch> <fx> <fxval>` previews an effect edit without writing.

## CLI Pattern Quick Reference

- `pattern transpose <semitones>` transposes all notes in the full pattern.
- `pattern transpose <semitones> <from> <to> [ch] [step <n>]` transposes a range and optional channel/row stride.
- `pattern transpose dry <semitones> <from> <to> [ch] [step <n>]` previews a transpose without writing.
- `pattern transpose dry preview <semitones> <from> <to> [ch] [step <n>]` previews affected row/channel note changes.
- `pattern transpose dry preview verbose <semitones> <from> <to> [ch] [step <n>]` adds per-step instrument/velocity/effect context.
- `pattern velocity <percent> <from> <to> [ch] [step <n>]` scales note velocity in a range (rounded, clamped 1..127).
- `pattern velocity dry preview verbose <percent> <from> <to> [ch] [step <n>]` previews per-step velocity changes with note/instrument/effect context.
- `pattern gate <percent> <from> <to> [ch] [step <n>]` scales gate ticks in a range (rounded, clamped to valid uint32).
- `pattern gate dry preview verbose <percent> <from> <to> [ch] [step <n>]` previews per-step gate changes with note/instrument/effect context.
- `pattern effect <fx> <fxval> <from> <to> [ch] [step <n>]` fills effect command/value across notes in a range.
- `pattern effect dry preview verbose <fx> <fxval> <from> <to> [ch] [step <n>]` previews per-step effect changes with note/instrument/velocity context.
- `pattern copy <from> <to> [chFrom] [chTo]` copies a row/channel block into the internal clipboard.
- `pattern paste <destRow> [channelOffset]` pastes copied notes; dry/preview/verbose modes are supported.
- `pattern humanize <velRange> <gateRangePercent> <seed> <from> <to> [ch] [step <n>]` adds seeded random variation to velocity/gate.
- `pattern randomize <probabilityPercent> <seed> <from> <to> [ch] [step <n>]` randomizes velocity/effect on selected steps.
- `pattern undo` and `pattern redo` provide one-level bulk edit undo/redo for committed pattern bulk operations.

## CLI MIDI Quick Reference

- `midi quick` shows a short overview of instrument, transport, and clock state.
- `midi quick all` prints the overview and then expands transport and clock quick sections.
- `midi quick compact` prints one-line compact transport/clock summaries.
- `midi quick compact <name>` prints compact output for a named MIDI endpoint when available.
- `midi transport quick` shows transport state plus timeout and source endpoint.
- `midi clock quick` shows clock source and autoconnect status.
- `midi transport toggle` toggles between running/stopped MIDI transport.

Example session:

```text
> midi quick
MIDI quick:
	instrument: 0
	transport: stopped
	clock: external (autoconnect off)

> midi transport toggle
MIDI transport started.

> midi quick all
MIDI quick all:
	...
MIDI transport quick:
	...
MIDI clock quick:
	...

> midi quick compact
MIDI quick compact: transport=running timeout=2000ms clock=external autoconnect=off
```

## Next milestones

1. Implement full LV2 port mapping and DSP bridge for discovered plugin IDs.
2. Add command chaining and tracker-compatible edge-case behavior across channels.
3. Extend plugin API coverage for effect processors.
4. Add PipeWire/JACK backends beside ALSA.

## Release process

- See docs/RELEASE_CHECKLIST.md for a lightweight release flow, tag steps, and issue seed list.
