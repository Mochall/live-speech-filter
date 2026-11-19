cmake_minimum_required(VERSION 3.24)
project(live-speech-filter-lite VERSION 0.10.3 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Shared recognition/muting code lives in the plugin source tree (upstream patch 0033).
set(UPSTREAM "${CMAKE_CURRENT_SOURCE_DIR}/../upstream/obs-profanity-filter" CACHE PATH "obs-profanity-filter source")
set(SHERPA_ONNX_ROOT "" CACHE PATH "sherpa-onnx v1.12.10 win-x64 shared package (include, lib, bin)")
set(FFMPEG_ROOT "" CACHE PATH "obs-deps folder with include/libswresample and lib/swresample.lib")
if(NOT SHERPA_ONNX_ROOT OR NOT FFMPEG_ROOT)
  message(FATAL_ERROR "set SHERPA_ONNX_ROOT and FFMPEG_ROOT")
endif()

if(MSVC)
  add_compile_options(/utf-8 /W3)
  # Same as the plugin: an older msvcp140.dll on the user's machine would crash on the
  # constexpr std::mutex constructor of MSVC 14.40+.
  add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CPP_PINYIN_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${UPSTREAM}/thirdparty/cpp-pinyin" cpp-pinyin)
if(MSVC)
  target_compile_options(cpp-pinyin PRIVATE /wd4251 /wd4267)
endif()

add_library(censor-core STATIC
  "${UPSTREAM}/src/censor-engine.cpp"
  "${UPSTREAM}/src/asr-model.cpp"
  "${UPSTREAM}/src/utils.cpp")
target_include_directories(censor-core PUBLIC "${UPSTREAM}/src" "${SHERPA_ONNX_ROOT}/include")
target_link_directories(censor-core PUBLIC "${SHERPA_ONNX_ROOT}/lib")
target_link_libraries(censor-core PUBLIC sherpa-onnx-c-api cpp-pinyin::cpp-pinyin)

# bzip2 1.0.8 (https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz, SHA256 ab5a0317…2269), decompression
# only: the recommended model is a .tar.bz2 and the tar.exe of Windows 10 cannot read bzip2.
set(BZIP2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../downloads/bzip2-1.0.8/bzip2-1.0.8" CACHE PATH "bzip2 1.0.8 sources")
add_library(bz2-decompress STATIC
  "${BZIP2_ROOT}/bzlib.c" "${BZIP2_ROOT}/decompress.c" "${BZIP2_ROOT}/huffman.c"
  "${BZIP2_ROOT}/crctable.c" "${BZIP2_ROOT}/randtable.c" "${BZIP2_ROOT}/blocksort.c" "${BZIP2_ROOT}/compress.c")
target_include_directories(bz2-decompress PUBLIC "${BZIP2_ROOT}")
target_compile_definitions(bz2-decompress PRIVATE BZ_NO_STDIO)
if(MSVC)
  target_compile_options(bz2-decompress PRIVATE /W0)
endif()

add_library(lite-core STATIC
  src/audio-io.cpp
  src/app-core.cpp
  src/swr-resampler.cpp)
target_include_directories(lite-core PUBLIC src "${FFMPEG_ROOT}/include")
target_link_libraries(lite-core PUBLIC censor-core
  "${FFMPEG_ROOT}/lib/swresample.lib" "${FFMPEG_ROOT}/lib/avutil.lib"
  ole32 mmdevapi avrt)

add_executable(lsf-lite-cli src/cli.cpp "${UPSTREAM}/src/companion-audio.cpp")
target_link_libraries(lsf-lite-cli PRIVATE lite-core)

# Test player (raw mode, bypasses device sound enhancements) for the file playback test.
add_executable(lsf-play-test src/play-test.cpp)
target_link_libraries(lsf-play-test PRIVATE lite-core)

# Runtime files next to the program: DLLs, word list, pinyin dictionary.
set(RUNTIME_DLLS
  "${SHERPA_ONNX_ROOT}/lib/sherpa-onnx-c-api.dll"
  "${SHERPA_ONNX_ROOT}/bin/onnxruntime.dll"
  "${SHERPA_ONNX_ROOT}/bin/onnxruntime_providers_shared.dll"
  "${FFMPEG_ROOT}/bin/swresample-5.dll"
  "${FFMPEG_ROOT}/bin/avutil-59.dll")
add_custom_command(TARGET lsf-lite-cli POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUNTIME_DLLS} "$<TARGET_FILE_DIR:lsf-lite-cli>"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:lsf-lite-cli>/data"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${UPSTREAM}/data/builtin_dirty_words.txt" "$<TARGET_FILE_DIR:lsf-lite-cli>/data/"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${UPSTREAM}/thirdparty/cpp-pinyin/res/dict" "$<TARGET_FILE_DIR:lsf-lite-cli>/data/dict")

# Window program (Qt 6 Widgets). LSF_QT_ROOT: the obs-deps Qt 6 package used for the plugin.
set(LSF_QT_ROOT "${UPSTREAM}/.deps/obs-deps-qt6-2025-07-11-x64" CACHE PATH "Qt 6 prefix")
list(APPEND CMAKE_PREFIX_PATH "${LSF_QT_ROOT}")
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
set(CMAKE_AUTOMOC ON)

add_executable(LiveSpeechFilter WIN32
  src/gui/bz2-unpack.cpp
  src/gui/bz2-unpack.hpp
  src/gui/cable-setup.cpp
  src/gui/cable-setup.hpp
  src/gui/main.cpp
  src/gui/streaming-apps.cpp
  src/gui/streaming-apps.hpp
  src/gui/lite-window.cpp
  src/gui/lite-window.hpp
  src/gui/tray-popup.cpp
  src/gui/tray-popup.hpp
  src/gui/ui-kit.cpp
  src/gui/ui-kit.hpp
  src/pipeline.cpp
  src/lite-checks.cpp
  "${UPSTREAM}/src/companion-audio.cpp"
  "${UPSTREAM}/src/streaming-apps.cpp"
  "${UPSTREAM}/src/update-check.cpp")
target_sources(LiveSpeechFilter PRIVATE resources/icons.qrc resources/app.rc)
set_source_files_properties(resources/app.rc PROPERTIES COMPILE_DEFINITIONS
  "LSF_VERSION_COMMA=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0;LSF_VERSION_STR=\"${PROJECT_VERSION}\"")
target_compile_definitions(LiveSpeechFilter PRIVATE LSF_VERSION="${PROJECT_VERSION}")
# Test builds only: the one latest.json address (http://127.0.0.1 allowed). Empty for real builds.
set(LSF_UPDATE_TEST_URL "" CACHE STRING "test address of latest.json (test builds only)")
if(LSF_UPDATE_TEST_URL)
  target_compile_definitions(LiveSpeechFilter PRIVATE LSF_UPDATE_TEST_URL="${LSF_UPDATE_TEST_URL}")
  message(WARNING "Update check test build: ${LSF_UPDATE_TEST_URL}")
endif()
target_include_directories(LiveSpeechFilter PRIVATE src src/gui)
target_link_libraries(LiveSpeechFilter PRIVATE lite-core bz2-decompress Qt6::Widgets Qt6::Network winmm dwmapi wintrust crypt32 winhttp)
set_target_properties(LiveSpeechFilter PROPERTIES AUTOMOC ON AUTORCC ON)

# Sponsor QR (Qt resource :/sponsor/sponsor-qr.png), compiled in as in the plugin so it cannot be
# swapped by replacing a file. The image is not part of the sources; without it the 支持项目 page
# says so.
set(LSF_SPONSOR_IMAGE "" CACHE FILEPATH "PNG shown on the 支持项目 page")
if(LSF_SPONSOR_IMAGE AND EXISTS "${LSF_SPONSOR_IMAGE}")
  file(TO_CMAKE_PATH "${LSF_SPONSOR_IMAGE}" _sponsor_image)
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sponsor.qrc"
       "<RCC><qresource prefix=\"/sponsor\"><file alias=\"sponsor-qr.png\">${_sponsor_image}</file></qresource></RCC>\n")
  target_sources(LiveSpeechFilter PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/sponsor.qrc")
  message(STATUS "Sponsor image embedded: ${_sponsor_image}")
endif()

add_custom_command(TARGET LiveSpeechFilter POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${RUNTIME_DLLS} "$<TARGET_FILE_DIR:LiveSpeechFilter>"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LSF_QT_ROOT}/bin/Qt6Core.dll" "${LSF_QT_ROOT}/bin/Qt6Gui.dll"
          "${LSF_QT_ROOT}/bin/Qt6Widgets.dll" "${LSF_QT_ROOT}/bin/Qt6Network.dll" "$<TARGET_FILE_DIR:LiveSpeechFilter>"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LiveSpeechFilter>/tls"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LSF_QT_ROOT}/plugins/tls/qschannelbackend.dll" "$<TARGET_FILE_DIR:LiveSpeechFilter>/tls/"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LiveSpeechFilter>/platforms"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LSF_QT_ROOT}/plugins/platforms/qwindows.dll" "$<TARGET_FILE_DIR:LiveSpeechFilter>/platforms/"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LiveSpeechFilter>/styles"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LSF_QT_ROOT}/plugins/styles/qmodernwindowsstyle.dll" "$<TARGET_FILE_DIR:LiveSpeechFilter>/styles/"
  COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LiveSpeechFilter>/data"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "${UPSTREAM}/data/builtin_dirty_words.txt" "$<TARGET_FILE_DIR:LiveSpeechFilter>/data/"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${UPSTREAM}/thirdparty/cpp-pinyin/res/dict" "$<TARGET_FILE_DIR:LiveSpeechFilter>/data/dict"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data/sounds" "$<TARGET_FILE_DIR:LiveSpeechFilter>/data/sounds")
