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
