# CrispPlay additions. Hooked into the root CMakeLists.txt with one
# `add_subdirectory(custom)` line (VEYRA_CUSTOM). Branding.cmake is included
# earlier, right after project(), because it must set the version label first.

# Feature code (custom/src, custom/include) will be added here phase by phase.

if(TARGET Qt6::Test)
  add_executable(crispplay_branding_tests tests/BrandingTests.cpp)
  target_link_libraries(crispplay_branding_tests PRIVATE Qt6::Core Qt6::Test)
  set_target_properties(crispplay_branding_tests PROPERTIES AUTOMOC ON)
endif()

# --- Instant replay (Phase 4, custom/docs/REPLAY.md) --------------------------
# Upstream targets only include crispplay/replay/Hooks.h (header-only), so
# nothing below is linked into them; the app links the two libraries.
if(TARGET veyra_engine AND TARGET veyra_qml_ui)
  add_library(crispplay_replay STATIC
    src/replay/ReplayService.cpp
    src/replay/ReplayMux.cpp)
  target_compile_features(crispplay_replay PRIVATE cxx_std_20)
  target_compile_options(crispplay_replay PRIVATE /utf-8)
  target_compile_definitions(crispplay_replay PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(crispplay_replay PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_link_libraries(crispplay_replay PUBLIC veyra_engine PRIVATE ${FFMPEG_LIBRARIES} dxgi)

  add_library(crispplay_replay_ui STATIC src/replay/ReplayController.cpp src/replay/ReplayController.h)
  set_target_properties(crispplay_replay_ui PROPERTIES AUTOMOC ON)
  target_compile_features(crispplay_replay_ui PRIVATE cxx_std_20)
  target_compile_options(crispplay_replay_ui PRIVATE /utf-8)
  target_compile_definitions(crispplay_replay_ui PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(crispplay_replay_ui PUBLIC crispplay_replay Qt6::Core Qt6::Qml)
  target_link_libraries(veyra_qml_ui PRIVATE crispplay_replay_ui)
  # Our QML is compiled in (qrc:/crispplay/...); upstream's stays on disk.
  qt_add_resources(veyra_qml_ui "crispplay_replay_qml" PREFIX "/crispplay" BASE "${CMAKE_CURRENT_SOURCE_DIR}/qml"
    FILES qml/ReplayHost.qml qml/ReplaySettings.qml)

  # ffprobe-like check of a saved replay (real-pipeline test, custom/scripts/replay-pipeline-test.ps1).
  add_executable(crispplay_replay_clip_check tests/ReplayClipCheck.cpp)
  target_compile_features(crispplay_replay_clip_check PRIVATE cxx_std_20)
  target_include_directories(crispplay_replay_clip_check PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_link_libraries(crispplay_replay_clip_check PRIVATE ${FFMPEG_LIBRARIES})
endif()

if(TARGET Qt6::Test)
  add_executable(crispplay_replay_ring_tests tests/ReplayRingTests.cpp)
  target_compile_features(crispplay_replay_ring_tests PRIVATE cxx_std_20)
  target_compile_options(crispplay_replay_ring_tests PRIVATE /utf-8)
  target_link_libraries(crispplay_replay_ring_tests PRIVATE Qt6::Core Qt6::Test)
  set_target_properties(crispplay_replay_ring_tests PROPERTIES AUTOMOC ON)
endif()
