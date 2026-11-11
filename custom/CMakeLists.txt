# CrispPlay additions. Hooked into the root CMakeLists.txt with one
# `add_subdirectory(custom)` line (VEYRA_CUSTOM). Branding.cmake is included
# earlier, right after project(), because it must set the version label first.

# Feature code (custom/src, custom/include) will be added here phase by phase.

if(TARGET Qt6::Test)
  add_executable(crispplay_branding_tests tests/BrandingTests.cpp)
  target_link_libraries(crispplay_branding_tests PRIVATE Qt6::Core Qt6::Test)
  set_target_properties(crispplay_branding_tests PROPERTIES AUTOMOC ON)
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
