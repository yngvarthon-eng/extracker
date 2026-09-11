#pragma once

#include <cstddef>
#include <initializer_list>
#include <string>

inline std::size_t countOccurrences(const std::string& haystack, const std::string& needle) {
  if (needle.empty()) {
    return 0;
  }

  std::size_t count = 0;
  std::size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

inline bool containsAll(const std::string& haystack,
                        std::initializer_list<std::string> needles) {
  for (const auto& needle : needles) {
    if (haystack.find(needle) == std::string::npos) {
      return false;
    }
  }
  return true;
}

inline bool containsAny(const std::string& haystack,
                        std::initializer_list<std::string> needles) {
  for (const auto& needle : needles) {
    if (haystack.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}
