#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class ExTrackerApp;
class PatternGrid;

class MainWindow : public juce::DocumentWindow {
public:
  explicit MainWindow(ExTrackerApp& app);
  ~MainWindow() override;

  void notifyPatternChanged();
  void closeButtonPressed() override;

private:
  ExTrackerApp& app;
  std::unique_ptr<juce::Component> content;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};
