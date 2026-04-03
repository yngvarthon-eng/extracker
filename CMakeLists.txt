cmake_minimum_required(VERSION 3.16)
project(exTracker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(EXTRACKER_ENABLE_ALSA "Enable ALSA output backend" ON)

find_package(Threads REQUIRED)

if(EXTRACKER_ENABLE_ALSA)
  find_package(PkgConfig QUIET)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(ALSA QUIET alsa)
  endif()
endif()

add_library(extracker_core
  src/audio_engine.cpp
  src/midi_input.cpp
  src/pattern_editor.cpp
  src/pattern_templates.cpp
  src/record_workflow.cpp
  src/sequencer.cpp
  src/transport.cpp
  src/plugin_host.cpp
)

target_include_directories(extracker_core PRIVATE include)

target_compile_options(extracker_core PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

target_link_libraries(extracker_core PRIVATE Threads::Threads)

if(ALSA_FOUND)
  target_compile_definitions(extracker_core PRIVATE EXTRACKER_HAVE_ALSA=1)
  target_include_directories(extracker_core PRIVATE ${ALSA_INCLUDE_DIRS})
  target_link_libraries(extracker_core PRIVATE ${ALSA_LIBRARIES})
  target_compile_options(extracker_core PRIVATE ${ALSA_CFLAGS_OTHER})
endif()

add_executable(extracker
  src/main.cpp
  src/command_registry.cpp
  src/core_cli.cpp
  src/default_command_bindings.cpp
  src/midi_cli.cpp
  src/note_cli.cpp
  src/pattern_cli.cpp
  src/plugin_cli.cpp
  src/record_cli.cpp
)
target_link_libraries(extracker PRIVATE extracker_core)
target_include_directories(extracker PRIVATE include)

include(CTest)
if(BUILD_TESTING)
  add_executable(extracker_smoke_test tests/smoke_test.cpp)
  target_include_directories(extracker_smoke_test PRIVATE include)
  target_link_libraries(extracker_smoke_test PRIVATE extracker_core)
  add_test(NAME extracker_smoke_test COMMAND extracker_smoke_test)

  add_executable(extracker_effect_regression_test tests/effect_regression_test.cpp)
  target_include_directories(extracker_effect_regression_test PRIVATE include)
  target_link_libraries(extracker_effect_regression_test PRIVATE extracker_core)
  add_test(NAME extracker_effect_regression_test COMMAND extracker_effect_regression_test)

  add_executable(extracker_lv2_port_map_scaffold_test tests/lv2_port_map_scaffold_test.cpp)
  target_include_directories(extracker_lv2_port_map_scaffold_test PRIVATE include)
  target_link_libraries(extracker_lv2_port_map_scaffold_test PRIVATE extracker_core)
  add_test(NAME extracker_lv2_port_map_scaffold_test COMMAND extracker_lv2_port_map_scaffold_test)

  add_executable(extracker_midi_clock_cli_logic_test
    tests/midi_clock_cli_logic_test.cpp
    src/midi_cli.cpp
  )
  target_include_directories(extracker_midi_clock_cli_logic_test PRIVATE include)
  target_link_libraries(extracker_midi_clock_cli_logic_test PRIVATE extracker_core)
  add_test(NAME extracker_midi_clock_cli_logic_test COMMAND extracker_midi_clock_cli_logic_test)

  add_executable(extracker_cli_record_roundtrip_test tests/cli_record_roundtrip_test.cpp)
  add_dependencies(extracker_cli_record_roundtrip_test extracker)
  add_test(NAME extracker_cli_record_roundtrip_test COMMAND extracker_cli_record_roundtrip_test)

  add_executable(extracker_cli_save_load_default_ext_test tests/cli_save_load_default_ext_test.cpp)
  add_dependencies(extracker_cli_save_load_default_ext_test extracker)
  add_test(NAME extracker_cli_save_load_default_ext_test COMMAND extracker_cli_save_load_default_ext_test)

  add_executable(extracker_cli_record_jump_test tests/cli_record_jump_test.cpp)
  add_dependencies(extracker_cli_record_jump_test extracker)
  add_test(NAME extracker_cli_record_jump_test COMMAND extracker_cli_record_jump_test)

  add_executable(extracker_cli_record_channel_test tests/cli_record_channel_test.cpp)
  add_dependencies(extracker_cli_record_channel_test extracker)
  add_test(NAME extracker_cli_record_channel_test COMMAND extracker_cli_record_channel_test)

  add_executable(extracker_cli_record_cursor_test tests/cli_record_cursor_test.cpp)
  add_dependencies(extracker_cli_record_cursor_test extracker)
  add_test(NAME extracker_cli_record_cursor_test COMMAND extracker_cli_record_cursor_test)

  add_executable(extracker_cli_midi_map_test tests/cli_midi_map_test.cpp)
  add_dependencies(extracker_cli_midi_map_test extracker)
  add_test(NAME extracker_cli_midi_map_test COMMAND extracker_cli_midi_map_test)

  add_executable(extracker_cli_midi_transport_test tests/cli_midi_transport_test.cpp)
  add_dependencies(extracker_cli_midi_transport_test extracker)
  add_test(NAME extracker_cli_midi_transport_test COMMAND extracker_cli_midi_transport_test)

  add_executable(extracker_cli_record_on_cursor_test tests/cli_record_on_cursor_test.cpp)
  add_dependencies(extracker_cli_record_on_cursor_test extracker)
  add_test(NAME extracker_cli_record_on_cursor_test COMMAND extracker_cli_record_on_cursor_test)

  add_executable(extracker_cli_record_cursor_relative_test tests/cli_record_cursor_relative_test.cpp)
  add_dependencies(extracker_cli_record_cursor_relative_test extracker)
  add_test(NAME extracker_cli_record_cursor_relative_test COMMAND extracker_cli_record_cursor_relative_test)

  add_executable(extracker_cli_record_cursor_step_test tests/cli_record_cursor_step_test.cpp)
  add_dependencies(extracker_cli_record_cursor_step_test extracker)
  add_test(NAME extracker_cli_record_cursor_step_test COMMAND extracker_cli_record_cursor_step_test)

  add_executable(extracker_cli_record_note_default_instr_test tests/cli_record_note_default_instr_test.cpp)
  add_dependencies(extracker_cli_record_note_default_instr_test extracker)
  add_test(NAME extracker_cli_record_note_default_instr_test COMMAND extracker_cli_record_note_default_instr_test)

  add_executable(extracker_cli_record_note_vel_test tests/cli_record_note_vel_test.cpp)
  add_dependencies(extracker_cli_record_note_vel_test extracker)
  add_test(NAME extracker_cli_record_note_vel_test COMMAND extracker_cli_record_note_vel_test)

  add_executable(extracker_cli_record_note_fx_test tests/cli_record_note_fx_test.cpp)
  add_dependencies(extracker_cli_record_note_fx_test extracker)
  add_test(NAME extracker_cli_record_note_fx_test COMMAND extracker_cli_record_note_fx_test)

  add_executable(extracker_cli_record_note_keyword_test tests/cli_record_note_keyword_test.cpp)
  add_dependencies(extracker_cli_record_note_keyword_test extracker)
  add_test(NAME extracker_cli_record_note_keyword_test COMMAND extracker_cli_record_note_keyword_test)

  add_executable(extracker_cli_record_note_invalid_test tests/cli_record_note_invalid_test.cpp)
  add_dependencies(extracker_cli_record_note_invalid_test extracker)
  add_test(NAME extracker_cli_record_note_invalid_test COMMAND extracker_cli_record_note_invalid_test)

  add_executable(extracker_cli_record_note_dry_test tests/cli_record_note_dry_test.cpp)
  add_dependencies(extracker_cli_record_note_dry_test extracker)
  add_test(NAME extracker_cli_record_note_dry_test COMMAND extracker_cli_record_note_dry_test)

  add_executable(extracker_cli_note_set_dry_test tests/cli_note_set_dry_test.cpp)
  add_dependencies(extracker_cli_note_set_dry_test extracker)
  add_test(NAME extracker_cli_note_set_dry_test COMMAND extracker_cli_note_set_dry_test)

  add_executable(extracker_cli_note_clear_dry_test tests/cli_note_clear_dry_test.cpp)
  add_dependencies(extracker_cli_note_clear_dry_test extracker)
  add_test(NAME extracker_cli_note_clear_dry_test COMMAND extracker_cli_note_clear_dry_test)

  add_executable(extracker_cli_note_modifiers_dry_test tests/cli_note_modifiers_dry_test.cpp)
  add_dependencies(extracker_cli_note_modifiers_dry_test extracker)
  add_test(NAME extracker_cli_note_modifiers_dry_test COMMAND extracker_cli_note_modifiers_dry_test)
endif()
