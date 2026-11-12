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
set(_ct2_libs ctranslate2 dnnl cpu_features sentencepiece)
set(CRISPPLAY_HAVE_CT2 ON)
foreach(f include/ctranslate2/translator.h include/sentencepiece_processor.h)
  if(NOT EXISTS "${_ct2}/${f}")
    set(CRISPPLAY_HAVE_CT2 OFF)
  endif()
endforeach()
foreach(l ${_ct2_libs})
  if(NOT EXISTS "${_ct2}/lib/${l}.lib")
    set(CRISPPLAY_HAVE_CT2 OFF)
  endif()
endforeach()
if(NOT CRISPPLAY_HAVE_CT2)
  message(STATUS "CrispPlay: CTranslate2 not found in ${_ct2}; subtitle translation (crispplay_subs_mt) skipped. "
                 "Run custom/scripts/build-ctranslate2.ps1 or set CRISPPLAY_CT2_ROOT.")
endif()

if(CRISPPLAY_HAVE_CT2)
  # All static, /MT like the app. OpenMP comes from /openmp in the CTranslate2 and
  # oneDNN objects (vcomp.lib -> vcomp140.dll, which ships next to the EXE).
  add_library(crispplay_ct2 INTERFACE IMPORTED GLOBAL)
  set(_ct2_paths "")
  foreach(l ${_ct2_libs})
    list(APPEND _ct2_paths "${_ct2}/lib/${l}.lib")
  endforeach()
  set_target_properties(crispplay_ct2 PROPERTIES
    INTERFACE_LINK_LIBRARIES "${_ct2_paths}"
    INTERFACE_INCLUDE_DIRECTORIES "${_ct2}/include")

  add_library(crispplay_subs_mt STATIC
    src/OpusMtTranslator.cpp
    include/crispplay/subs/mt/OpusMtTranslator.h
    include/crispplay/subs/mt/TranslatorInterface.h)
  target_compile_options(crispplay_subs_mt PRIVATE /utf-8)
  target_compile_definitions(crispplay_subs_mt PRIVATE NOMINMAX)
  target_link_libraries(crispplay_subs_mt PUBLIC crispplay_subs_mt_text PRIVATE crispplay_ct2)

  if(TARGET Qt6::Core)
    add_executable(crispplay_mt_bench tools/MtBench.cpp)
    target_compile_options(crispplay_mt_bench PRIVATE /utf-8)
    target_compile_definitions(crispplay_mt_bench PRIVATE NOMINMAX
      CRISPPLAY_MT_DEFAULT_MODEL="${CRISPPLAY_MT_MODEL_DIR}"
      CRISPPLAY_MT_DEFAULT_SENTENCES="${PROJECT_SOURCE_DIR}/custom/subs/spike/sentences.json"
      CRISPPLAY_MT_DEFAULT_REAL="${PROJECT_SOURCE_DIR}/../deps/subs-real/real.json")
    target_link_libraries(crispplay_mt_bench PRIVATE crispplay_subs_mt Qt6::Core psapi)
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
  endif()
endif()
