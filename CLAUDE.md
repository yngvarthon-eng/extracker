# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

exTracker is a Linux-first, pattern-based music tracker (C++17 + CMake). It ships two
executables built from one shared core library (`extracker_core`):

- `extracker` — a REPL-style CLI (the primary surface; almost all tests drive it).
- `extracker_gui` — a JUCE-based GUI (JUCE is fetched at configure time via FetchContent).

## Build & test

```bash
# Configure (preferred: Ninja)
cmake -S . -B build -G Ninja
# Fallback if Ninja is unavailable
cmake -S . -B build-make -G "Unix Makefiles"

# Build everything (CLI, GUI, tests)
cmake --build build

# Build just the CLI (skips the JUCE fetch/build — much faster)
cmake --build build --target extracker

# Run the whole suite
ctest --test-dir build --output-on-failure

# Run a single test by exact name or regex
ctest --test-dir build -R extracker_cli_core_status_json_test --output-on-failure
```

Notes:
- Build dirs (all ignored via `/build*/`):
  - `build-gui/`: Debug build of CLI and GUI; `./launch-gui.sh` runs (and first builds) the GUI here.
  - `build-make/`: the local, untracked `./extracker` wrapper script builds/runs the CLI here.
  - `build-release/`: Release build that `./package.sh` configures and packages into a tarball.
  - `build/` and `build-make-gui/` are older dirs; `build-make-gui/` was configured under a
    previous checkout path and no longer works (`package.sh` refuses such dirs).
- The GUI target pulls JUCE 7.0.10 from GitHub on first configure and links `curl`. A clean
  configure that touches the GUI is slow; prefer `--target extracker` when working on core/CLI.
- Audio backends are auto-detected via pkg-config and gated by CMake options
  `EXTRACKER_ENABLE_ALSA/JACK/PIPEWIRE` (all ON by default). JACK is opened with
  `JackNoStartServer`, so without a running jackd it falls through to ALSA immediately
  (letting libjack spawn jackd stalled every start and every CLI test by ~6 s). Missing dev packages just disable
  that backend; the Null backend is always available, so builds/tests work headless.

## How the tests work (important)

There are ~180 tests. Two distinct styles:
- **Core tests** link `extracker_core` directly and call the C++ API (e.g. `module_*_test`,
  `sample_*_test`, `effect_regression_test`).
- **CLI tests** (`cli_*` / `extracker_cli_*`) `popen("./extracker", ...)`, pipe a script of
  commands ending in `quit`, and assert on stdout substrings. They depend on the `extracker`
  target and run with the build dir as the working directory (that's why `./extracker` resolves).
  When adding CLI behavior, add a matching `cli_*` test **and** register it in `CMakeLists.txt`
  (`add_executable` + `add_dependencies(... extracker)` + `add_test`).

## Architecture

### Core engine (`src/*.cpp`, `include/extracker/*.hpp` → `extracker_core`)

Real-time-ish playback pipeline, all driven by a background thread:

- **Transport** — owns a clock thread; converts tempo/ticks-per-beat/ticks-per-row into a
  monotonic tick counter and current row with pattern wraparound.
- **Sequencer** — `update(editor, transport, audio, plugins)` reads the transport's current row,
  diffs against the previous row, and emits note-on/note-off **deltas**. It implements the tracker
  effect runtime (arpeggio, slides, vibrato, retrigger, volume, Fxx speed/tempo, extended `E_`
  commands, per-channel effect memory). Effects logic lives in `sequencer.cpp` and is the most
  test-covered area (see `effect_regression_test` and many `cli_*` effect tests).
- **AudioEngine** — backend abstraction; auto-probe order PipeWire → JACK → ALSA → Null. Holds
  persistent voices keyed by MIDI note with attack/release envelopes, and pulls rendered samples
  from the PluginHost for plugin-assigned instruments.
- **PatternEditor** — a fixed rows×channels grid of `Step` (note, instrument, sample slot, gate
  ticks, velocity, retrigger, effect command/value). All edits are bounds-checked and MIDI-range
  validated.
- **Module** — owns multiple `PatternEditor`s plus the song order (sequence of pattern indices),
  per-pattern swing, and module message. `currentEditor()` is what the CLI/GUI edit live.
- **PluginHost** — instrument slots (assign a plugin id like `builtin.sine`/`builtin.square` to an
  instrument number), sample slots, a plugin registry/factory layer, and an external-adapter
  scaffold with an LV2 manifest backend (`dlopen` + `lv2_descriptor` probing; audio still falls
  back to built-in synthesis until the full LV2 port bridge lands).
  There are 256 instrument slots (`kInstrumentSlotCount` in `instrument_mix.hpp`; every
  per-instrument table uses it — loop over slots with a `std::size_t`, never a `std::uint8_t`).
  A sample instrument linked to a sample-bank slot (`assignSampleSlotToInstrument`) shares that
  slot's `SampleAsset` (data + root/gain/loop), so bank edits reach every linked instrument;
  each plugin keeps its own voices. The audio thread renders into reusable
  `InstrumentMixBuffers` and only touches instruments that are assigned or sounding.
- **MidiInput** — ALSA-seq based MIDI in, with clock/transport sync and channel→instrument mapping.
- **ChannelManager** (`channel_manager.hpp`) — the single owner of per-channel user state: name,
  default instrument, mute, solo, volume and base filter. Solo wins over mute (`isAudible`). The
  sequencer takes it as `update(..., const ChannelManager*)` and reads mute/solo/volume/filter from
  it; the GUI hands the playback thread a copy per tick. Pattern effects 18/19/1A override the base
  filter only in the sequencer's running state, which `Sequencer::reset()` clears.
- **Channel layout** (`channel_layout.hpp`) — insert/delete/move/duplicate a channel across *all*
  patterns plus `ChannelManager`, keeping each pattern's channel count. `ChannelEditResult::sources`
  maps new→old channel so callers can carry other per-channel state (record channel, cursor, ...).
  `ChannelLayoutHistory` is the undo/redo for these edits; it refuses to undo once any pattern
  changed since, so it never discards later note edits.
- **PatternClipboard / ClipboardHistory** (`pattern_clipboard.hpp`) — the one clipboard used by
  both CLI and GUI: copy a block (with row stride), `plan()` a paste (modes Overwrite/Merge/Mix/
  Insert, flood, column mask) as a list of cell changes, then `apply()` it. CLI dry-run previews
  print the plan, so previews and real pastes cannot drift. `writeStep` writes a cell exactly.
  `ClipboardHistory` keeps the last 9 copies, newest first, deduped by content.

### CLI command system (`src/*_cli.cpp`, `src/main.cpp`)

- `main.cpp` is the composition root: it constructs all engine objects, owns the shared mutable
  state (loop range, record state, MIDI maps, song mode), starts the sequencer thread, and runs
  the read-eval loop. Lines are split on `;` for command chaining.
- Dispatch is a `CommandRegistry` (`unordered_map<string, handler>`). `createDefaultCommandBindings`
  wires top-level verbs to per-domain handlers; `registerCommandHandlers` installs them.
- Handlers are split by domain: `core_cli` (transport/status/reset/save/load), `note_cli`,
  `pattern_cli` (+ `_basic`/`_bulk`/`_shared`), `record_cli`, `midi_cli`, `plugin_cli`,
  `sample_cli`, `sample_editor_cli`, `module_cli` (pattern/song management), `channel_cli`
  (`channel list|vol|mute|solo|name|insert|delete|move|dup|undo|redo`).
- Handlers receive a **context struct** (`CoreCommandContext`, `PatternCommandContext`,
  `RecordCommandContext`, `MidiCommandContext`, `ModuleCommandContext`, `ChannelCommandContext`) that threads references to
  the shared state and engine objects. To add a command: add the handler in the right `*_cli.cpp`,
  and if it needs new shared state, extend the relevant context struct and its construction in
  `main.cpp`.
- `pattern` and `song` verbs are special-cased in `main.cpp`'s `executeCommandLine` to route
  between pattern-editing commands and pattern/song-management commands.

### GUI (`src/gui/`)

`ExTrackerApp` (a `juce::JUCEApplication`) owns its **own** copies of the same core engine objects
and its own sequencer thread, reusing `extracker_core` for all the actual logic. `pattern_grid`
renders the tracker grid; `main_window` hosts it.

### Save/load & file format

Modules are plain-text. Current magic is `EXTRACKER_SONG_V1` (multi-pattern + song order); legacy
`EXTRACKER_PATTERN_V1/V2` and `EXTRACKER_MODULE_V1/V2` single-pattern formats still load. Default
extension is `.xtp`. **The serializer/deserializer is implemented twice** — in `main.cpp`
(`savePatternToFile`/`loadPatternFromFile`) and again in `src/gui/app.cpp`. CLI↔GUI round-trip
parity is a maintained invariant (there are dedicated compat tests, e.g.
`cli_core_gui_song_tail_token_compat_test`). When you change the format, update **both** writers
and keep the tail-token parsing in sync. The GUI has two loaders in `app.cpp` (song and legacy
pattern), so there are three tail-token parsers in total, plus an `isSongTailToken` list in each file.

Per-channel and global lines go through shared helpers instead of hand-written loops; prefer adding
new song state the same way:
- `writeChannelState` / `applyChannelFileToken` / `isChannelFileToken` (`channel_manager.hpp`):
  `CHANNEL_INSTRUMENTS`, `CHANNEL_MUTED`, `CHANNEL_VOLUME`, `CHANNEL_NAMES` (quoted),
  `CHANNEL_FILTERS` (`type cutoff resonance` per channel, 0–255). Solo is not saved.
- `writeReverbLine` / `readReverbLine` (`song_bundle.hpp`): `REVERB room damping wet width`, 0–255.
- Loaders reset `ChannelManager`, channel undo history and reverb before reading a file, so songs
  without these lines do not inherit the previous song's state.

## Conventions

- Everything lives in `namespace extracker` (GUI classes are global, e.g. `ExTrackerApp`).
- The GUI playback thread runs `sequencer.update` *outside* `stateMutex` on snapshots. Do not call
  mutating `Sequencer` methods from the message thread; put state in `ChannelManager` (under the
  lock) or set a flag such as `ExTrackerApp::sequencerResetRequested` that the playback thread acts on.
- Core sources compile with `-Wall -Wextra -Wpedantic`; keep them warning-clean.
- CLI commands have terse aliases (e.g. `p`/`s`/`st`/`w`/`r`, `pattern dup`/`sw`/`ls`/`del`),
  each with its own `cli_*_alias_test`. Adding/renaming an alias means adding/adjusting that test.
- Dry-run + preview is a pervasive pattern for destructive edits: most `note`/`pattern` mutators
  accept `dry [preview [verbose]]` to validate and show the diff without writing.
