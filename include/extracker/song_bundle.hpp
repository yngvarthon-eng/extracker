#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace extracker {

// Shared path/bundling helpers for the .xtp save/load code paths in
// src/main.cpp (CLI) and src/gui/app.cpp (GUI). Both serializers keep their
// own token read/write loops (per CLAUDE.md), but rely on these same pure
// filesystem primitives so CLI- and GUI-saved songs behave identically.

// Replaces anything that isn't alnum/-/_ with '_'. Falls back to
// "sample" if the result would be empty.
std::string sanitizeFileToken(std::string text);

// Resolves a path stored in a .xtp file (possibly relative) against the
// directory the song file lives in, and normalizes it.
std::string resolveStoredPath(const std::filesystem::path& moduleDirectory,
                               const std::string& storedPath);

// Copies a single file referenced by `sourcePath` (resolved against
// moduleDirectory if relative) into `<moduleDirectory>/<songStem>_<bundleDirName>/`,
// named "<slotLabel>_<sanitized name><ext>". Repeated calls with the same
// resolved source path reuse the previous copy via `dedupe` (keyed by the
// normalized absolute source path). Returns the path to store in the .xtp
// file (relative to moduleDirectory on success, or the original `sourcePath`
// unchanged if bundling wasn't possible).
std::string bundleFile(const std::string& sourcePath,
                        const std::filesystem::path& moduleDirectory,
                        const std::filesystem::path& songStem,
                        const std::string& bundleDirName,
                        const std::string& slotLabel,
                        const std::string& displayName,
                        std::unordered_map<std::string, std::string>& dedupe);

// Copies the entire parent directory of `sourceFilePath` (resolved against
// moduleDirectory if relative) into
// `<moduleDirectory>/<songStem>_<bundleDirName>/<slotLabel>_<sanitized name>/`,
// preserving its internal structure. Use for formats (SFZ, XPM) that
// reference sibling sample files by path relative to themselves. Returns the
// path to the copied representative file to store in the .xtp file
// (relative to moduleDirectory on success, or `sourceFilePath` unchanged if
// bundling wasn't possible).
std::string bundleDirectory(const std::string& sourceFilePath,
                             const std::filesystem::path& moduleDirectory,
                             const std::filesystem::path& songStem,
                             const std::string& bundleDirName,
                             const std::string& slotLabel,
                             const std::string& displayName,
                             std::unordered_map<std::string, std::string>& dedupe);

// -- Instrument plugin-id parsing -------------------------------------------
//
// An instrument's PluginHost::pluginForInstrument() id can be a bare plugin
// id (builtin.sine, vst3.<hex>, lv2:<uri> — none of these embed a
// filesystem path and can't be bundled) or it can embed a path to a file we
// *can* bundle: "sf2:<path>[:melodic:N]" (single self-contained file),
// "sfz:<path>" or a bare "<path>.sfz"/".xpm" (references sibling sample
// files by path relative to itself — bundle the whole directory), or a bare
// "<path>.s3i"/".xi"/".iff"/".8svx" (single self-contained file).
enum class InstrumentIdKind {
  NotBundlable,
  SingleFile,
  DirectoryOfSiblingFiles,
};

struct InstrumentIdParts {
  InstrumentIdKind kind = InstrumentIdKind::NotBundlable;
  std::string prefix;  // e.g. "sf2:", "sfz:", or "" for a bare path
  std::string path;    // filesystem path portion (only meaningful when bundlable)
  std::string suffix;  // e.g. ":melodic:36", or ""
};

// For a NotBundlable id, `path` is set to the full original id unchanged.
InstrumentIdParts parseInstrumentId(const std::string& pluginId);

// Reassembles an id from `parts` with `newPath` swapped in for the path
// portion (unchanged prefix/suffix). For a NotBundlable `parts`, `newPath`
// is ignored and `parts.path` (the original id) is returned as-is.
std::string rebuildInstrumentId(const InstrumentIdParts& parts, const std::string& newPath);

}  // namespace extracker
