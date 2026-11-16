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

# --- MP4 check tool ---------------------------------------------------------------
# ffprobe-like check of a saved clip (upstream instant replay / recording), used by
# custom/scripts/replay-test.ps1. CrispPlay's own instant replay was removed in
# favour of upstream's (2.1.6+); see custom/docs/REPLAY.md.
if(TARGET veyra_engine AND TARGET veyra_qml_ui)
  add_executable(crispplay_replay_clip_check tests/ReplayClipCheck.cpp)
  target_compile_features(crispplay_replay_clip_check PRIVATE cxx_std_20)
  target_include_directories(crispplay_replay_clip_check PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_link_libraries(crispplay_replay_clip_check PRIVATE ${FFMPEG_LIBRARIES})
endif()

# --- Game subtitle translation (Phase 5): text core (AGPL-3.0 port of CouchSubs), engine, OCR ---
add_subdirectory(subs/core)
add_subdirectory(subs/net) # Phase 5 model download + optional online engine (custom/subs/net/README.md)
add_subdirectory(subs/mt) # Phase 5 subtitle translation engine (custom/subs/mt/README.md)
add_subdirectory(subs/ocr) # Phase 5 OCR readers (custom/subs/ocr/README.md)
add_subdirectory(subs/app) # Phase 5 app integration: capture, overlay, settings (keep last: uses the targets above)

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
  # 2.1.9: the dusk palette's logo, home-page wordmark and the startup intro's layers
  # (DuskIntro.qml) are images too; custom/branding/dusk holds ours under upstream's file
  # names (custom/scripts/make-brand-dusk.py), so the intro's mechanics stay upstream's.
  file(GLOB crispplay_dusk_images CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/branding/dusk/*.png")
  add_custom_target(crispplay_logo ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/branding/logo-256.png" "${CMAKE_BINARY_DIR}/qml/Veyra/logo.png"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${crispplay_dusk_images} "${CMAKE_BINARY_DIR}/qml/Veyra"
    COMMENT "CrispPlay logo")
  add_dependencies(crispplay_logo veyra_qml_files)
  add_dependencies(veyra_qml_ui crispplay_logo)
endif()
