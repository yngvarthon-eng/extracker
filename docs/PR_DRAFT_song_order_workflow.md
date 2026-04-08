## Summary

This PR adds a full song/module workflow across core, CLI, and GUI.

- Adds module-level song-order model and mutation APIs.
- Adds full-song persistence (`EXTRACKER_SONG_V1`) including `SONG_ORDER`.
- Adds Song vs Pattern playback behavior and song-order-driven progression.
- Adds GUI song-order editor controls and list preview.
- Adds focused tests for song-order invariants and CLI save/load roundtrip.
- Stabilizes smoke/plugin scan tests to avoid environment/timing brittleness.

## Key Changes

### Core

- Added song-order data and operations to module model.
- Kept song order coherent across pattern insert/remove operations.

### CLI

- Save/load now reads and writes `EXTRACKER_SONG_V1` with `SONG_ORDER`.
- Legacy compatibility for older pattern/module formats is retained.

### GUI

- Added Pattern/Song play modes.
- Song playback now follows song-order positions.
- Added song-order editor controls and visual order preview.
- Updated save/load wording to song/module semantics.

### Tests

- Added `module_song_order_test` for mutation and index invariants.
- Added `extracker_cli_song_roundtrip_test` for full-song save/load markers.
- Stabilized smoke test 0x06 and note-off assertions.
- Made plugin scan test robust to environments with zero newly discovered plugins.

## Validation

Executed in `build-make`:

- `ctest --output-on-failure`

Result: `104/104` tests passing.

## Commits

- `177047c` feat: add song-order workflow, GUI controls, and robust tests
- `3fae102` docs: note song/module workflow status

## Follow-ups

- Optional: split large feature commit into narrower thematic commits in future work (core/GUI/tests).
- Optional: add dedicated GUI integration tests once harness is available.
