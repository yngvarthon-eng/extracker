# Architecture Draft

## Core modules

- AudioEngine: real-time output, timing source, render callback.
- PatternEditor: tracker grid state, note/effect entry, pattern operations.
- PluginHost: instrument/effect loading and processing graph.

## CLI command modules

- main.cpp: bootstrap/composition root (runtime setup, command binding, thread lifecycle).
- core_cli: transport/status/reset/save/load command handling.
- midi_cli: MIDI command handling and event processing helpers.
- note_cli: note set/clear/vel/gate/fx command parsing and writes.
- pattern_cli: pattern print/play/template command handling.
- record_cli: record command parsing, write workflow, undo/redo integration.
- plugin_cli: help output, plugin commands, and sine convenience command.

## Near-term design notes

- Keep audio thread lock-free where possible.
- Route UI edits through command queues into audio-safe state updates.
- Start with fixed song tempo and pattern length, then generalize.

## Pattern editor milestone 2

- Implemented: in-memory fixed-size grid with rows x channels.
- Implemented: step operations for insert, query, and clear note events.
- Implemented: MIDI note range validation (0-127) and bounds-safe editing.
- Implemented: per-step instrument, gate, velocity, retrigger, and effect command/value columns.
- Implemented: sequencer runtime handling for `0x00`, `0x01`, `0x02`, `0x03`, `0x04`, combined `0x05` and `0x06`, `0x09`, `0x0A`, `0x0B`, `0x0C`, `0x0D`, `0x0F`, and extended `0x0E` subcommands (`E1`, `E2`, `E9`, `EC`, `ED`).
- Implemented: per-channel effect memory when effect value is zero.
- Next: improve command chaining and tracker-compatible edge cases across channels.

## Transport milestone 3

- Implemented: transport module with tempo and ticks-per-beat settings.
- Implemented: play/stop lifecycle with a dedicated clock thread.
- Implemented: monotonic tick counter for sequencing timing.
- Implemented: row advancement based on ticks-per-row with pattern wraparound.

## Sequencer bridge milestone

- Implemented: sequencer update loop that reads transport currentRow.
- Implemented: row event dispatch scanning pattern channels for note data.
- Implemented: MIDI note to Hz conversion feeding audio engine test tone frequency.
- Implemented: polyphonic row dispatch (multiple channel notes per row).
- Implemented: note-off behavior by sending an empty voice set on empty rows.

## Voice lifecycle milestone

- Implemented: delta-based note-on/note-off dispatch between rows.
- Implemented: persistent voice state keyed by MIDI note in audio engine.
- Implemented: basic attack/release envelope handling for active/releasing voices.
- Implemented: per-step gate tick release within a row.
- Implemented: per-step velocity scaling and retrigger handling.
- Implemented: per-step instrument ID routing for independent note groups.
- Implemented: plugin host instrument-slot routing for instrument IDs.
- Implemented: sequencer note-on/note-off callback routing into plugin host.
- Implemented: audio engine rendering from plugin-owned voice state for mapped instruments.
- Implemented: audio engine pull-based render callback into plugin host for mapped instruments.
- Implemented: per-slot plugin instances with note callbacks and render callback interface.
- Implemented: per-slot plugin parameter set/get API.
- Implemented: plugin registry/factory discovery model for plugin IDs.
- Implemented: external plugin adapter scaffold behind registry factories.
- Implemented: default disabled external adapter placeholder for deterministic startup.
- Implemented: LV2 manifest adapter backend that discovers plugin URIs and registers factories.
- Implemented: LV2 shared-library probe path (`dlopen` + `lv2_descriptor`) during plugin instance creation.
- Next: add LV2 port mapping and audio/MIDI bridge to replace fallback synthesis processing.
