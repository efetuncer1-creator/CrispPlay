# CrispPlay additions. Hooked into the root CMakeLists.txt with one
# `add_subdirectory(custom)` line (VEYRA_CUSTOM). Branding.cmake is included
# earlier, right after project(), because it must set the version label first.

# Feature code (custom/src, custom/include) will be added here phase by phase.

# Turkish interface language (custom/include/crispplay/I18n.h): compiled in next to
# upstream's catalog, read by UiLanguage.cpp for the "tr" code.
if(TARGET veyra_qml_ui)
  qt_add_resources(veyra_qml_ui "crispplay_i18n" PREFIX "/crispplay/i18n"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/i18n" FILES "${CMAKE_CURRENT_SOURCE_DIR}/i18n/tr.json")
endif()

if(TARGET Qt6::Test)
  add_executable(crispplay_branding_tests tests/BrandingTests.cpp)
  target_link_libraries(crispplay_branding_tests PRIVATE Qt6::Core Qt6::Test)
  set_target_properties(crispplay_branding_tests PROPERTIES AUTOMOC ON)

  if(TARGET veyra_ui_i18n)
    add_executable(crispplay_i18n_tests tests/I18nTests.cpp)
    target_compile_options(crispplay_i18n_tests PRIVATE /utf-8)
    target_link_libraries(crispplay_i18n_tests PRIVATE veyra_ui_i18n Qt6::Core Qt6::Test)
    set_target_properties(crispplay_i18n_tests PROPERTIES AUTOMOC ON)
    qt_add_resources(crispplay_i18n_tests "crispplay_i18n_upstream" PREFIX "/i18n"
      BASE "${PROJECT_SOURCE_DIR}/i18n" FILES "${PROJECT_SOURCE_DIR}/i18n/catalog.json")
    qt_add_resources(crispplay_i18n_tests "crispplay_i18n_tr" PREFIX "/crispplay/i18n"
      BASE "${CMAKE_CURRENT_SOURCE_DIR}/i18n" FILES "${CMAKE_CURRENT_SOURCE_DIR}/i18n/tr.json")
  endif()
endif()

# --- Discord Rich Presence (Phase 3) ------------------------------------------
# Core (payload + pipe client) needs only Qt Core; the controller adds QML.
if(TARGET Qt6::Core)
  add_library(crispplay_discord_core STATIC src/discord/Presence.cpp src/discord/IpcClient.cpp)
  target_include_directories(crispplay_discord_core PUBLIC include)
  target_compile_features(crispplay_discord_core PUBLIC cxx_std_20)
  target_compile_options(crispplay_discord_core PRIVATE /utf-8)
  target_link_libraries(crispplay_discord_core PUBLIC Qt6::Core)
endif()

if(TARGET veyra_qml_ui)
  add_library(crispplay_discord STATIC src/discord/Controller.cpp include/crispplay/discord/Controller.h)
  target_compile_options(crispplay_discord PRIVATE /utf-8)
  target_link_libraries(crispplay_discord PUBLIC crispplay_discord_core Qt6::Qml)
  set_target_properties(crispplay_discord PROPERTIES AUTOMOC ON)
  target_link_libraries(veyra_qml_ui PRIVATE crispplay_discord)

  # Our QML lives next to the Veyra module as its own folder (build/qml/CrispPlay);
  # the root CMakeLists copies qml/ into build/qml, this adds ours beside it.
  add_custom_target(crispplay_qml_files ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/qml ${CMAKE_BINARY_DIR}/qml/CrispPlay
    COMMENT "Copying CrispPlay QML")
  add_dependencies(veyra_qml_ui crispplay_qml_files)
endif()

if(TARGET Qt6::Test)
  add_executable(crispplay_discord_tests tests/DiscordTests.cpp)
  target_link_libraries(crispplay_discord_tests PRIVATE crispplay_discord_core Qt6::Test)
  target_compile_options(crispplay_discord_tests PRIVATE /utf-8)
  set_target_properties(crispplay_discord_tests PROPERTIES AUTOMOC ON)
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

# --- Game subtitle translation, text core (Phase 5b, AGPL-3.0 port of CouchSubs) ---
add_subdirectory(subs/core)

# --- CrispPlay logo -------------------------------------------------------------
# No upstream hook: this file runs after the root CMakeLists has copied
# assets/veyra.ico and qml/Veyra/logo.png into the build tree, so ours are
# written over those copies (EXE/window icon and the in-app logo).
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/branding/crispplay.ico" "${CMAKE_BINARY_DIR}/veyra.ico" COPYONLY)
foreach(rc veyra-version.rc veyra-qml-version.rc)
  if(EXISTS "${CMAKE_BINARY_DIR}/${rc}")
    set_property(SOURCE "${CMAKE_BINARY_DIR}/${rc}" DIRECTORY "${PROJECT_SOURCE_DIR}"
      APPEND PROPERTY OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/branding/crispplay.ico")
  endif()
endforeach()
if(TARGET veyra_qml_files)
  # veyra_qml_files copies qml/ on every build; replace its logo afterwards.
  add_custom_target(crispplay_logo ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/branding/logo-256.png" "${CMAKE_BINARY_DIR}/qml/Veyra/logo.png"
    COMMENT "CrispPlay logo")
  add_dependencies(crispplay_logo veyra_qml_files)
  add_dependencies(veyra_qml_ui crispplay_logo)
endif()
