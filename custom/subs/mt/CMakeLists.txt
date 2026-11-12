# Subtitle translation engine (Phase 5, step 5a): OPUS-MT on CTranslate2 + SentencePiece.
# Hooked from custom/CMakeLists.txt with add_subdirectory(subs/mt). README.md here has the
# build steps; custom/scripts/build-ctranslate2.ps1 produces CRISPPLAY_CT2_ROOT.

# Pure text helpers: no runtime dependency, always built (the tests use them).
add_library(crispplay_subs_mt_text STATIC src/TextSplit.cpp include/crispplay/subs/mt/TextSplit.h)
target_include_directories(crispplay_subs_mt_text PUBLIC include)
target_compile_features(crispplay_subs_mt_text PUBLIC cxx_std_20)
target_compile_options(crispplay_subs_mt_text PRIVATE /utf-8)

set(CRISPPLAY_CT2_ROOT "${PROJECT_SOURCE_DIR}/../deps/ctranslate2-install" CACHE PATH
  "Prefix with the MKL-free CTranslate2 + SentencePiece build (custom/scripts/build-ctranslate2.ps1)")
set(CRISPPLAY_MT_MODEL_DIR "${PROJECT_SOURCE_DIR}/../deps/models/opus-mt-tc-big-en-tr-ct2-int8" CACHE PATH
  "OPUS-MT CTranslate2 model used by the tests and crispplay_mt_bench (development only)")

set(_ct2 "${CRISPPLAY_CT2_ROOT}")
if(EXISTS "${_ct2}/include/ctranslate2/translator.h" AND EXISTS "${_ct2}/lib/ctranslate2.lib"
   AND EXISTS "${_ct2}/bin/ctranslate2.dll" AND EXISTS "${_ct2}/lib/sentencepiece.lib"
   AND EXISTS "${_ct2}/include/sentencepiece_processor.h")
  set(CRISPPLAY_HAVE_CT2 ON)
else()
  set(CRISPPLAY_HAVE_CT2 OFF)
  message(STATUS "CrispPlay: CTranslate2 not found in ${_ct2}; subtitle translation (crispplay_subs_mt) skipped. "
                 "Run custom/scripts/build-ctranslate2.ps1 or set CRISPPLAY_CT2_ROOT.")
endif()

if(CRISPPLAY_HAVE_CT2)
  add_library(crispplay_ct2 SHARED IMPORTED GLOBAL)
  set_target_properties(crispplay_ct2 PROPERTIES
    IMPORTED_LOCATION "${_ct2}/bin/ctranslate2.dll"
    IMPORTED_IMPLIB "${_ct2}/lib/ctranslate2.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${_ct2}/include")
  add_library(crispplay_sentencepiece STATIC IMPORTED GLOBAL)
  set_target_properties(crispplay_sentencepiece PROPERTIES
    IMPORTED_LOCATION "${_ct2}/lib/sentencepiece.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${_ct2}/include")

  add_library(crispplay_subs_mt STATIC
    src/OpusMtTranslator.cpp
    include/crispplay/subs/mt/OpusMtTranslator.h
    include/crispplay/subs/mt/TranslatorInterface.h)
  target_compile_options(crispplay_subs_mt PRIVATE /utf-8)
  target_compile_definitions(crispplay_subs_mt PRIVATE NOMINMAX)
  target_link_libraries(crispplay_subs_mt PUBLIC crispplay_subs_mt_text PRIVATE crispplay_ct2 crispplay_sentencepiece)
  # The DLL travels with the library's users; vcomp140.dll (MSVC OpenMP) comes with the VC runtime.
  set(CRISPPLAY_CT2_DLL "${_ct2}/bin/ctranslate2.dll" CACHE INTERNAL "")

  if(TARGET Qt6::Core)
    add_executable(crispplay_mt_bench tools/MtBench.cpp)
    target_compile_options(crispplay_mt_bench PRIVATE /utf-8)
    target_compile_definitions(crispplay_mt_bench PRIVATE NOMINMAX
      CRISPPLAY_MT_DEFAULT_MODEL="${CRISPPLAY_MT_MODEL_DIR}"
      CRISPPLAY_MT_DEFAULT_SENTENCES="${PROJECT_SOURCE_DIR}/custom/subs/spike/sentences.json"
      CRISPPLAY_MT_DEFAULT_REAL="${PROJECT_SOURCE_DIR}/../deps/subs-real/real.json")
    target_link_libraries(crispplay_mt_bench PRIVATE crispplay_subs_mt Qt6::Core psapi)
    add_custom_command(TARGET crispplay_mt_bench POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CRISPPLAY_CT2_DLL}" "$<TARGET_FILE_DIR:crispplay_mt_bench>")
  endif()
endif()

if(TARGET Qt6::Test)
  add_executable(crispplay_subs_mt_tests tests/MtTests.cpp)
  target_compile_options(crispplay_subs_mt_tests PRIVATE /utf-8)
  target_link_libraries(crispplay_subs_mt_tests PRIVATE crispplay_subs_mt_text Qt6::Core Qt6::Test)
  set_target_properties(crispplay_subs_mt_tests PROPERTIES AUTOMOC ON)
  if(CRISPPLAY_HAVE_CT2)
    target_compile_definitions(crispplay_subs_mt_tests PRIVATE CRISPPLAY_HAVE_CT2
      CRISPPLAY_MT_DEFAULT_MODEL="${CRISPPLAY_MT_MODEL_DIR}")
    target_link_libraries(crispplay_subs_mt_tests PRIVATE crispplay_subs_mt)
    add_custom_command(TARGET crispplay_subs_mt_tests POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CRISPPLAY_CT2_DLL}" "$<TARGET_FILE_DIR:crispplay_subs_mt_tests>")
  endif()
endif()
