# crispplay_subs_net: subtitle model download (ModelStore) and the optional online
# translation engines (OnlineTranslator). custom/subs/net/README.md. GPL-3.0.
find_package(Qt6 QUIET COMPONENTS Core Network ZlibPrivate)
if(NOT TARGET Qt6::Network OR NOT TARGET Qt6::ZlibPrivate)
  message(STATUS "CrispPlay subs net: Qt Network / Qt zlib not found; crispplay_subs_net skipped")
  return()
endif()

add_library(crispplay_subs_net STATIC
  include/crispplay/subs/ModelManifest.h
  include/crispplay/subs/ModelStore.h
  include/crispplay/subs/OnlineTranslator.h
  src/ModelStore.cpp
  src/ModelManifestData.inc
  src/ZipReader.cpp
  src/ZipReader.h
  src/WinHttpPost.cpp
  src/WinHttpPost.h
  src/OnlineTranslator.cpp
  src/OnlineInternal.h)
target_include_directories(crispplay_subs_net PUBLIC include
  PRIVATE src "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
target_compile_features(crispplay_subs_net PUBLIC cxx_std_20)
target_compile_options(crispplay_subs_net PRIVATE /utf-8 /W4)
target_compile_definitions(crispplay_subs_net PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
# Qt6::ZlibPrivate: Qt's bundled zlib, exported by Qt6Core (z_ prefix); no extra DLL.
target_link_libraries(crispplay_subs_net PUBLIC crispplay_subs_core Qt6::Core Qt6::Network
  PRIVATE Qt6::ZlibPrivate winhttp crypt32)
set_target_properties(crispplay_subs_net PROPERTIES AUTOMOC ON)

if(TARGET Qt6::Test)
  add_executable(crispplay_subs_net_tests tests/NetTests.cpp tests/TestHttpServer.h tests/TestHttpServer.cpp)
  target_include_directories(crispplay_subs_net_tests PRIVATE src)
  target_compile_options(crispplay_subs_net_tests PRIVATE /utf-8)
  target_compile_definitions(crispplay_subs_net_tests PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(crispplay_subs_net_tests PRIVATE crispplay_subs_net Qt6::Test Qt6::Network Qt6::ZlibPrivate)
  set_target_properties(crispplay_subs_net_tests PROPERTIES AUTOMOC ON)
endif()
