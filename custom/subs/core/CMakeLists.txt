# CrispPlay subtitle text core (Phase 5b): std-only C++20 port of the CouchSubs text rules.
# Most sources are AGPL-3.0-only ports, see NOTICE.md. Hooked in from custom/CMakeLists.txt.

file(GLOB crispplay_subs_core_sources CONFIGURE_DEPENDS src/*.cpp)
add_library(crispplay_subs_core STATIC ${crispplay_subs_core_sources})
target_include_directories(crispplay_subs_core PUBLIC include)
target_compile_features(crispplay_subs_core PUBLIC cxx_std_20)
if(MSVC)
  target_compile_options(crispplay_subs_core PRIVATE /utf-8 /W4)
endif()

file(GLOB crispplay_subs_core_test_sources CONFIGURE_DEPENDS tests/*.cpp)
add_executable(crispplay_subs_core_tests ${crispplay_subs_core_test_sources})
target_link_libraries(crispplay_subs_core_tests PRIVATE crispplay_subs_core)
target_compile_definitions(crispplay_subs_core_tests PRIVATE
  CRISPPLAY_SUBS_TEST_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests"
  CRISPPLAY_SUBS_REAL_DIR="${PROJECT_SOURCE_DIR}/../deps/subs-real")
if(MSVC)
  target_compile_options(crispplay_subs_core_tests PRIVATE /utf-8)
endif()
