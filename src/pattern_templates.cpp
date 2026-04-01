#include "extracker/pattern_templates.hpp"

#include <algorithm>
#include <cstdint>

#include "extracker/pattern_editor.hpp"

namespace extracker {

namespace {

void insertTemplateNote(PatternEditor& editor, int row, int channel, int note, int instrument, int velocity) {
  editor.insertNote(
      row,
      channel,
      note,
      static_cast<std::uint8_t>(std::clamp(instrument, 0, 255)),
      0,
      static_cast<std::uint8_t>(std::clamp(velocity, 1, 127)),
      false,
      0,
      0);
}

}  // namespace

void clearPattern(PatternEditor& editor) {
  for (std::size_t row = 0; row < editor.rows(); ++row) {
    for (std::size_t channel = 0; channel < editor.channels(); ++channel) {
      editor.clearStep(static_cast<int>(row), static_cast<int>(channel));
    }
  }
}

bool applyPatternTemplate(PatternEditor& editor, const std::string& name) {
  clearPattern(editor);

  if (name == "blank") {
    return true;
  }

  if (name == "house") {
    for (int row = 0; row < static_cast<int>(editor.rows()); row += 4) {
      insertTemplateNote(editor, row, 0, 36, 0, 120);
    }
    for (int row = 4; row < static_cast<int>(editor.rows()); row += 8) {
      insertTemplateNote(editor, row, 1, 38, 1, 112);
    }
    for (int row = 2; row < static_cast<int>(editor.rows()); row += 2) {
      insertTemplateNote(editor, row, 2, 42, 1, 84);
    }
    insertTemplateNote(editor, 0, 3, 48, 0, 96);
    insertTemplateNote(editor, 4, 3, 50, 0, 92);
    insertTemplateNote(editor, 8, 3, 53, 0, 96);
    insertTemplateNote(editor, 12, 3, 55, 0, 92);
    return true;
  }

  if (name == "electro") {
    insertTemplateNote(editor, 0, 0, 36, 0, 122);
    insertTemplateNote(editor, 6, 0, 36, 0, 104);
    insertTemplateNote(editor, 8, 0, 36, 0, 118);
    insertTemplateNote(editor, 14, 0, 36, 0, 104);
    insertTemplateNote(editor, 4, 1, 38, 1, 108);
    insertTemplateNote(editor, 12, 1, 40, 1, 108);
    for (int row = 0; row < static_cast<int>(editor.rows()); row += 2) {
      insertTemplateNote(editor, row, 2, (row % 4 == 0) ? 42 : 46, 1, 78);
    }
    insertTemplateNote(editor, 0, 3, 43, 0, 96);
    insertTemplateNote(editor, 3, 3, 46, 0, 88);
    insertTemplateNote(editor, 7, 3, 50, 0, 92);
    insertTemplateNote(editor, 10, 3, 46, 0, 88);
    insertTemplateNote(editor, 12, 3, 41, 0, 96);
    return true;
  }

  return false;
}

std::vector<std::string> availablePatternTemplates() {
  return {"blank", "house", "electro"};
}

}  // namespace extracker