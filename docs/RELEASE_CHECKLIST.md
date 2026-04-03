# Release Checklist

This checklist is intentionally lightweight for the current prototype stage.

## 1) Pre-Release Validation

- Confirm working tree is clean.
- Build succeeds.
- Full test suite passes.
- Verify CLI basics manually:
  - Start app
  - Save and load pattern files
  - MIDI transport status and reset commands

Suggested commands:

- git status --short
- cmake --build build-make
- ctest --test-dir build-make --output-on-failure

## 2) Version and Tag

Choose a version tag (example: v0.1.0).

- Create annotated tag:
  - git tag -a v0.1.0 -m "exTracker v0.1.0"
- Push branch and tag:
  - git push
  - git push origin v0.1.0

## 3) Changelog Note Template

Use this short template in the GitHub release notes:

- Summary:
  - Short description of release scope.
- Added:
  - New commands/features.
- Changed:
  - Behavior updates or defaults.
- Fixed:
  - Regressions/bugs.
- Tests:
  - Build and test status.

Example draft:

- Summary:
  - Prototype milestone update for MIDI transport and pattern workflow.
- Added:
  - midi transport reset command.
  - CLI regression tests for MIDI transport and default save/load extension.
- Changed:
  - Extensionless save/load now defaults to .xtp.
- Fixed:
  - Improved coverage for CLI save/load behavior.
- Tests:
  - build-make build passes.
  - CTest suite passes.

## 4) Post-Release Sanity

- Open release page and confirm notes are readable.
- Confirm tag appears in repository tags.
- Pull from a fresh clone and run a build as a smoke check.

## 5) Next Milestone Issue Seed List

Create these as GitHub issues for the next iteration:

1. Implement LV2 audio/MIDI port mapping and runtime DSP bridge.
2. Add CLI command chaining and tracker edge-case command semantics.
3. Expand plugin API support for effect processors.
4. Add PipeWire backend option alongside ALSA/null backends.
5. Add focused tests for MIDI clock source discovery/autoconnect flows.
6. Add release automation helper script for tag + notes generation.
