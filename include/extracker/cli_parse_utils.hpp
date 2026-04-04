#pragma once

#include <sstream>
#include <string>

namespace extracker {
namespace cli {

inline bool parseStrictIntToken(const std::string& token, int& outValue) {
  std::istringstream parse(token);
  parse >> outValue;
  return static_cast<bool>(parse) && parse.eof();
}

inline bool parseStrictDoubleToken(const std::string& token, double& outValue) {
  std::istringstream parse(token);
  parse >> outValue;
  return static_cast<bool>(parse) && parse.eof();
}

inline bool parseStrictIntFromStream(std::istringstream& input, int& outValue) {
  std::string token;
  if (!(input >> token)) {
    return false;
  }
  return parseStrictIntToken(token, outValue);
}

inline bool parseStrictDoubleFromStream(std::istringstream& input, double& outValue) {
  std::string token;
  if (!(input >> token)) {
    return false;
  }
  return parseStrictDoubleToken(token, outValue);
}

inline bool hasExtraTokens(std::istringstream& input) {
  std::string extra;
  return static_cast<bool>(input >> extra);
}

}  // namespace cli
}  // namespace extracker
