#include "extracker/song_bundle.hpp"

#include <cctype>
#include <system_error>

namespace extracker {

std::string sanitizeFileToken(std::string text) {
  for (char& ch : text) {
    const bool isAlphaNum = (ch >= 'a' && ch <= 'z') ||
                            (ch >= 'A' && ch <= 'Z') ||
                            (ch >= '0' && ch <= '9');
    if (!isAlphaNum && ch != '-' && ch != '_') {
      ch = '_';
    }
  }
  if (text.empty()) {
    return "sample";
  }
  return text;
}

std::string resolveStoredPath(const std::filesystem::path& moduleDirectory,
                               const std::string& storedPath) {
  if (storedPath.empty()) {
    return storedPath;
  }

  std::filesystem::path resolved(storedPath);
  if (resolved.is_relative() && !moduleDirectory.empty()) {
    resolved = moduleDirectory / resolved;
  }

  std::error_code ec;
  const auto normalized = std::filesystem::weakly_canonical(resolved, ec);
  if (!ec) {
    return normalized.string();
  }
  return resolved.lexically_normal().string();
}

namespace {

std::filesystem::path resolveSourcePath(const std::string& sourcePath,
                                         const std::filesystem::path& moduleDirectory) {
  std::filesystem::path resolved(sourcePath);
  if (resolved.is_relative() && !moduleDirectory.empty()) {
    resolved = moduleDirectory / resolved;
  }
  return resolved;
}

std::string toStoredRelativeOrAbsolute(const std::filesystem::path& file,
                                        const std::filesystem::path& moduleDirectory) {
  std::error_code relError;
  const auto relativePath = std::filesystem::relative(file, moduleDirectory, relError);
  if (!relError && !relativePath.empty()) {
    return relativePath.generic_string();
  }
  return file.string();
}

// True if `a` and `b` name the same location, or one is nested inside the
// other (checked lexically, since the paths involved may not exist yet).
bool pathsOverlap(const std::filesystem::path& a, const std::filesystem::path& b) {
  auto contains = [](const std::filesystem::path& outer, const std::filesystem::path& inner) {
    std::error_code ec;
    const auto rel = std::filesystem::relative(inner, outer, ec);
    if (ec || rel.empty()) {
      return false;
    }
    const std::string relStr = rel.generic_string();
    return relStr == "." || (relStr != ".." && relStr.rfind("../", 0) != 0);
  };
  return contains(a, b) || contains(b, a);
}

}  // namespace

std::string bundleFile(const std::string& sourcePath,
                        const std::filesystem::path& moduleDirectory,
                        const std::filesystem::path& songStem,
                        const std::string& bundleDirName,
                        const std::string& slotLabel,
                        const std::string& displayName,
                        std::unordered_map<std::string, std::string>& dedupe) {
  if (sourcePath.empty() || moduleDirectory.empty()) {
    return sourcePath;
  }

  const auto resolvedSource = resolveSourcePath(sourcePath, moduleDirectory);

  std::error_code existsError;
  if (!std::filesystem::exists(resolvedSource, existsError) || existsError) {
    return sourcePath;
  }

  const std::string sourceKey = resolvedSource.lexically_normal().string();
  if (auto existing = dedupe.find(sourceKey); existing != dedupe.end()) {
    return existing->second;
  }

  const std::filesystem::path bundleDirectoryPath =
      moduleDirectory / (songStem.string() + "_" + bundleDirName);

  std::error_code createDirError;
  std::filesystem::create_directories(bundleDirectoryPath, createDirError);
  if (createDirError) {
    return sourcePath;
  }

  const std::string baseName =
      displayName.empty() ? resolvedSource.stem().string() : displayName;
  const std::filesystem::path targetFile =
      bundleDirectoryPath /
      (slotLabel + "_" + sanitizeFileToken(baseName) + resolvedSource.extension().string());

  if (pathsOverlap(resolvedSource, targetFile)) {
    // Re-saving an already-bundled song: the source is already the bundled
    // copy itself. Copying it onto itself would risk truncating the file
    // (open-for-write while still open-for-read) -- nothing to do, it's
    // already where it needs to be.
    const std::string stored = toStoredRelativeOrAbsolute(resolvedSource, moduleDirectory);
    dedupe.emplace(sourceKey, stored);
    return stored;
  }

  std::error_code copyError;
  std::filesystem::copy_file(resolvedSource, targetFile,
                              std::filesystem::copy_options::overwrite_existing, copyError);
  if (copyError) {
    return sourcePath;
  }

  const std::string stored = toStoredRelativeOrAbsolute(targetFile, moduleDirectory);
  dedupe.emplace(sourceKey, stored);
  return stored;
}

std::string bundleDirectory(const std::string& sourceFilePath,
                             const std::filesystem::path& moduleDirectory,
                             const std::filesystem::path& songStem,
                             const std::string& bundleDirName,
                             const std::string& slotLabel,
                             const std::string& displayName,
                             std::unordered_map<std::string, std::string>& dedupe) {
  if (sourceFilePath.empty() || moduleDirectory.empty()) {
    return sourceFilePath;
  }

  const auto resolvedSource = resolveSourcePath(sourceFilePath, moduleDirectory);

  std::error_code existsError;
  if (!std::filesystem::exists(resolvedSource, existsError) || existsError) {
    return sourceFilePath;
  }

  const auto sourceDir = resolvedSource.has_parent_path()
                              ? resolvedSource.parent_path()
                              : std::filesystem::path(".");
  const std::string sourceDirKey = sourceDir.lexically_normal().string();

  std::filesystem::path targetDir;
  if (auto existing = dedupe.find(sourceDirKey); existing != dedupe.end()) {
    targetDir = existing->second;
  } else {
    const std::filesystem::path bundleDirectoryPath =
        moduleDirectory / (songStem.string() + "_" + bundleDirName);

    const std::string baseName =
        displayName.empty() ? resolvedSource.stem().string() : displayName;
    targetDir = bundleDirectoryPath / (slotLabel + "_" + sanitizeFileToken(baseName));

    if (pathsOverlap(sourceDir, targetDir)) {
      // The source directory already contains (or equals, or is nested in)
      // where we'd copy it to -- e.g. the instrument file already lives
      // directly in moduleDirectory, or this is a re-save of an
      // already-bundled song. A recursive copy would try to copy the
      // freshly-created destination back into itself. Nothing to bundle:
      // the file is already at least as local to the song as bundling
      // would make it, so just reference it in place.
      dedupe.emplace(sourceDirKey, sourceDir.string());
      return toStoredRelativeOrAbsolute(resolvedSource, moduleDirectory);
    }

    std::error_code createDirError;
    std::filesystem::create_directories(bundleDirectoryPath, createDirError);
    if (createDirError) {
      return sourceFilePath;
    }

    std::error_code copyError;
    std::filesystem::copy(sourceDir, targetDir,
                           std::filesystem::copy_options::recursive |
                               std::filesystem::copy_options::overwrite_existing,
                           copyError);
    if (copyError) {
      return sourceFilePath;
    }

    dedupe.emplace(sourceDirKey, targetDir.string());
  }

  const std::filesystem::path targetFile = std::filesystem::path(targetDir) / resolvedSource.filename();
  return toStoredRelativeOrAbsolute(targetFile, moduleDirectory);
}

namespace {

std::string lowercaseExtension(const std::string& path) {
  const auto dot = path.rfind('.');
  if (dot == std::string::npos) {
    return {};
  }
  std::string ext = path.substr(dot);
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return ext;
}

}  // namespace

InstrumentIdParts parseInstrumentId(const std::string& pluginId) {
  InstrumentIdParts parts;

  if (pluginId.size() > 4 && pluginId.compare(0, 4, "sf2:") == 0) {
    const std::string rest = pluginId.substr(4);
    const auto melodicPos = rest.rfind(":melodic:");
    parts.kind = InstrumentIdKind::SingleFile;
    parts.prefix = "sf2:";
    if (melodicPos != std::string::npos) {
      parts.path = rest.substr(0, melodicPos);
      parts.suffix = rest.substr(melodicPos);
    } else {
      parts.path = rest;
    }
    return parts;
  }

  if (pluginId.size() > 4 && pluginId.compare(0, 4, "sfz:") == 0) {
    parts.kind = InstrumentIdKind::DirectoryOfSiblingFiles;
    parts.prefix = "sfz:";
    parts.path = pluginId.substr(4);
    return parts;
  }

  const std::string ext = lowercaseExtension(pluginId);
  if (ext == ".sfz" || ext == ".xpm") {
    parts.kind = InstrumentIdKind::DirectoryOfSiblingFiles;
    parts.path = pluginId;
    return parts;
  }
  if (ext == ".s3i" || ext == ".xi" || ext == ".iff" || ext == ".8svx") {
    parts.kind = InstrumentIdKind::SingleFile;
    parts.path = pluginId;
    return parts;
  }

  parts.kind = InstrumentIdKind::NotBundlable;
  parts.path = pluginId;
  return parts;
}

std::string rebuildInstrumentId(const InstrumentIdParts& parts, const std::string& newPath) {
  if (parts.kind == InstrumentIdKind::NotBundlable) {
    return parts.path;
  }
  return parts.prefix + newPath + parts.suffix;
}

}  // namespace extracker
