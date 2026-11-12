# crispplay_subs_ocr: subtitle band OCR (Phase 5, custom/docs/SUBS_SPIKE.md).
# Windows OCR (C++/WinRT, ships with Windows) always; the PP-OCR second reader
# needs ONNX Runtime headers at build time and onnxruntime.dll at run time.
# Without ONNX Runtime the library still builds and runs Windows-only.

set(CRISPPLAY_DEPS_DIR "${PROJECT_SOURCE_DIR}/../deps" CACHE PATH "CrispPlay third-party folder (see custom/BUILD.md)")
set(CRISPPLAY_ONNXRUNTIME_ROOT "${CRISPPLAY_DEPS_DIR}/onnxruntime-1.31.1" CACHE PATH
    "Official ONNX Runtime Windows x64 release folder (include/, lib/)")
set(CRISPPLAY_PPOCR_MODEL "${CRISPPLAY_DEPS_DIR}/models/ppocr-rec/PP-OCRv6_rec_small.onnx" CACHE FILEPATH
    "PP-OCR recognition model used by the bench and the tests")

set(_ort_ok OFF)
if(EXISTS "${CRISPPLAY_ONNXRUNTIME_ROOT}/include/onnxruntime_cxx_api.h" AND
   EXISTS "${CRISPPLAY_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
  set(_ort_ok ON)
  message(STATUS "CrispPlay subs OCR: ONNX Runtime at ${CRISPPLAY_ONNXRUNTIME_ROOT} (PP-OCR reader on)")
else()
  message(STATUS "CrispPlay subs OCR: ONNX Runtime not found at ${CRISPPLAY_ONNXRUNTIME_ROOT}; Windows OCR only")
endif()

add_library(crispplay_subs_ocr STATIC
  src/BandImage.cpp
  src/TextScore.cpp
  src/WindowsOcr.cpp
  src/PpOcr.cpp
  src/HybridOcr.cpp)
target_include_directories(crispplay_subs_ocr PUBLIC include)
target_compile_features(crispplay_subs_ocr PUBLIC cxx_std_20)
target_compile_options(crispplay_subs_ocr PRIVATE /utf-8 /EHsc)
target_compile_definitions(crispplay_subs_ocr PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(crispplay_subs_ocr PRIVATE windowsapp ole32)
if(_ort_ok)
  # Headers only: onnxruntime.dll is loaded with LoadLibraryEx at run time (no import library).
  target_include_directories(crispplay_subs_ocr PRIVATE "${CRISPPLAY_ONNXRUNTIME_ROOT}/include")
  target_compile_definitions(crispplay_subs_ocr PRIVATE CRISPPLAY_SUBS_HAVE_ORT=1)
else()
  target_compile_definitions(crispplay_subs_ocr PRIVATE CRISPPLAY_SUBS_HAVE_ORT=0)
endif()

# Copies onnxruntime.dll next to an executable that uses the PP-OCR reader.
function(crispplay_subs_ocr_runtime target)
  if(_ort_ok)
    add_custom_command(TARGET ${target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CRISPPLAY_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:${target}>"
      VERBATIM)
  endif()
endfunction()

if(TARGET Qt6::Test AND TARGET Qt6::Gui)
  add_executable(crispplay_subs_ocr_tests tests/OcrTests.cpp)
  target_compile_options(crispplay_subs_ocr_tests PRIVATE /utf-8)
  target_compile_definitions(crispplay_subs_ocr_tests PRIVATE CRISPPLAY_PPOCR_MODEL="${CRISPPLAY_PPOCR_MODEL}")
  target_link_libraries(crispplay_subs_ocr_tests PRIVATE crispplay_subs_ocr Qt6::Gui Qt6::Test)
  set_target_properties(crispplay_subs_ocr_tests PROPERTIES AUTOMOC ON)
  crispplay_subs_ocr_runtime(crispplay_subs_ocr_tests)
endif()

if(TARGET Qt6::Gui)
  add_executable(crispplay_ocr_bench tools/OcrBench.cpp)
  target_compile_options(crispplay_ocr_bench PRIVATE /utf-8)
  target_compile_definitions(crispplay_ocr_bench PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_compile_definitions(crispplay_ocr_bench PRIVATE
    CRISPPLAY_PPOCR_MODEL="${CRISPPLAY_PPOCR_MODEL}"
    CRISPPLAY_DEPS_DIR="${CRISPPLAY_DEPS_DIR}"
    CRISPPLAY_SPIKE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../spike")
  target_link_libraries(crispplay_ocr_bench PRIVATE crispplay_subs_ocr Qt6::Gui psapi)
  crispplay_subs_ocr_runtime(crispplay_ocr_bench)
endif()
