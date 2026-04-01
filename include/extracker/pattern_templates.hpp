#pragma once

#include <string>
#include <vector>

namespace extracker {

class PatternEditor;

void clearPattern(PatternEditor& editor);
bool applyPatternTemplate(PatternEditor& editor, const std::string& name);
std::vector<std::string> availablePatternTemplates();

}  // namespace extracker