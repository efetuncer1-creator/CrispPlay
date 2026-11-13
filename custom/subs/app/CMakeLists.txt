# Game subtitle translation in the app (Phase 5c/5d/5f, custom/docs/SUBS.md):
# band capture in the present chain, OCR + translation worker, overlay line,
# settings object. Hooked from custom/CMakeLists.txt with add_subdirectory(subs/app),
# which must stay after subs/core, subs/net, subs/mt and subs/ocr.
#
# The app builds without CTranslate2 (no local translator: the settings page says
# so) and without ONNX Runtime (Windows OCR only).
if(NOT (TARGET veyra_engine AND TARGET veyra_qml_ui))
  return()
endif()

set(_subs_net_include "${CMAKE_CURRENT_SOURCE_DIR}/../net/include")
set(_subs_app_sources
  src/BandFormat.cpp src/BandFormat.h
  src/BandCapture.cpp src/BandCapture.h
  src/SubsService.cpp src/SubsService.h
  src/OverlayRender.cpp src/OverlayRender.h
  src/RegionEdit.cpp src/RegionEdit.h
  src/SubsController.cpp src/SubsController.h)
if(NOT TARGET crispplay_subs_net)
  # Contract headers only: a stand-in without download and online engines.
  list(APPEND _subs_app_sources src/NetFallback.cpp
    "${_subs_net_include}/crispplay/subs/ModelStore.h" "${_subs_net_include}/crispplay/subs/OnlineTranslator.h"
    "${_subs_net_include}/crispplay/subs/ModelManifest.h")
  message(STATUS "CrispPlay subs app: crispplay_subs_net not in this build; model download and online engines are stubbed")
endif()

add_library(crispplay_subs_app STATIC ${_subs_app_sources})
set_target_properties(crispplay_subs_app PROPERTIES AUTOMOC ON)
target_compile_features(crispplay_subs_app PRIVATE cxx_std_20)
target_compile_options(crispplay_subs_app PRIVATE /utf-8)
target_compile_definitions(crispplay_subs_app PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(crispplay_subs_app PUBLIC src PRIVATE "${PROJECT_SOURCE_DIR}/apps/veyra/ui")
target_link_libraries(crispplay_subs_app PUBLIC crispplay_subs_core crispplay_subs_ocr veyra_engine Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
if(TARGET crispplay_subs_net)
  target_link_libraries(crispplay_subs_app PUBLIC crispplay_subs_net)
  target_compile_definitions(crispplay_subs_app PUBLIC CRISPPLAY_SUBS_HAVE_NET=1)
else()
  target_include_directories(crispplay_subs_app PUBLIC "${_subs_net_include}")
  target_compile_definitions(crispplay_subs_app PUBLIC CRISPPLAY_SUBS_HAVE_NET=0)
endif()
if(TARGET crispplay_subs_mt)
  target_link_libraries(crispplay_subs_app PUBLIC crispplay_subs_mt)
  target_compile_definitions(crispplay_subs_app PUBLIC CRISPPLAY_SUBS_HAVE_MT=1)
else()
  target_compile_definitions(crispplay_subs_app PUBLIC CRISPPLAY_SUBS_HAVE_MT=0)
  message(STATUS "CrispPlay subs app: no CTranslate2, the local translator is left out (online engine only)")
endif()

target_link_libraries(veyra_qml_ui PRIVATE crispplay_subs_app)
qt_add_resources(veyra_qml_ui "crispplay_subs_qml" PREFIX "/crispplay" BASE "${PROJECT_SOURCE_DIR}/custom/qml"
  FILES "${PROJECT_SOURCE_DIR}/custom/qml/SubsHost.qml" "${PROJECT_SOURCE_DIR}/custom/qml/SubsSettings.qml")

# Run-time files next to the EXE: ONNX Runtime (second OCR reader, loaded with
# LoadLibraryEx) and the MSVC OpenMP runtime (CTranslate2 / oneDNN; the CRT is
# linked statically, so the VC++ redistributable cannot be assumed).
set(_subs_runtime_files "")
if(EXISTS "${CRISPPLAY_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
  list(APPEND _subs_runtime_files "${CRISPPLAY_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll")
endif()
if(TARGET crispplay_subs_mt)
  set(_vcomp "")
  if(DEFINED ENV{VCToolsRedistDir})
    file(TO_CMAKE_PATH "$ENV{VCToolsRedistDir}" _redist)
    file(GLOB _vcomp_found "${_redist}/x64/Microsoft.VC*.OpenMP/vcomp140.dll")
    if(_vcomp_found)
      list(GET _vcomp_found 0 _vcomp)
    endif()
  endif()
  set(CRISPPLAY_VCOMP_DLL "${_vcomp}" CACHE FILEPATH "vcomp140.dll from the VC++ redistributable (OpenMP), copied next to the EXE")
  if(EXISTS "${CRISPPLAY_VCOMP_DLL}")
    list(APPEND _subs_runtime_files "${CRISPPLAY_VCOMP_DLL}")
  else()
    message(WARNING "CrispPlay subs app: vcomp140.dll not found (VCToolsRedistDir); the translator needs it next to the EXE")
  endif()
endif()
if(_subs_runtime_files)
  # The EXE lives in the root of the build tree (a POST_BUILD step cannot be added from this folder).
  add_custom_target(crispplay_subs_runtime ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_subs_runtime_files} "${CMAKE_BINARY_DIR}"
    COMMENT "CrispPlay subtitle runtime files (onnxruntime.dll, vcomp140.dll)" VERBATIM)
  add_dependencies(veyra_qml_ui crispplay_subs_runtime)
endif()

if(TARGET Qt6::Test)
  add_executable(crispplay_subs_app_tests tests/AppTests.cpp)
  target_compile_options(crispplay_subs_app_tests PRIVATE /utf-8)
  target_compile_definitions(crispplay_subs_app_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_include_directories(crispplay_subs_app_tests PRIVATE "${PROJECT_SOURCE_DIR}/apps/veyra/ui")
  target_link_libraries(crispplay_subs_app_tests PRIVATE crispplay_subs_app Qt6::Gui Qt6::Test d3d12 dxgi)
  set_target_properties(crispplay_subs_app_tests PROPERTIES AUTOMOC ON)
endif()
