cmake_minimum_required(VERSION 3.9.0)
project(looper VERSION 0.1.0 LANGUAGES CXX)
set(LOOPER_HINTS "${LOOPER_HINTS}")
if(NOT DEFINED CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE "Release")
endif()
if(LOOPER_HINTS)
  message(STATUS "Looking for libraries in ${LOOPER_HINTS}")
  if(CMAKE_BUILD_TYPE MATCHES "^Debug$")
    list(APPEND CMAKE_PREFIX_PATH
      ${LOOPER_HINTS}/debug
    )
    list(APPEND CMAKE_LIBRARY_PATH
      ${LOOPER_HINTS}/debug/lib/manual-link
    )
    list(APPEND CMAKE_FIND_ROOT_PATH
      ${LOOPER_HINTS}/debug
    )
  endif()
  list(APPEND CMAKE_PREFIX_PATH
    ${LOOPER_HINTS}
  )
  list(APPEND CMAKE_FIND_ROOT_PATH
    ${LOOPER_HINTS}
  )
  list(APPEND CMAKE_LIBRARY_PATH
    $${LOOPER_HINTS}/lib/manual-link
  )
endif()

find_package(Git)

find_path(OPUS_INCLUDE_DIR NAMES opus/opus.h)
find_library(OPUS_LIBRARY NAMES opus libopus)

find_path(OPUSFILE_INCLUDE_DIR NAMES opus/opusfile.h)
find_library(OPUSFILE_LIBRARY NAMES opusfile libopusfile)

find_path(MPG123_INCLUDE_DIR NAMES mpg123.h)
find_library(MPG123_LIBRARY  NAMES mpg123 libmpg123)

find_path(FLAC_INCLUDE_DIR FLAC/all.h)
find_library(FLAC_LIBRARY NAMES FLAC flac libFLAC libflac)

find_path(VORBIS_INCLUDE_DIR vorbis/vorbisfile.h)
find_path(OGG_INCLUDE_DIR ogg/ogg.h)

find_library(OGG_LIBRARY NAMES ogg libogg)
find_library(VORBIS_ENC_LIBRARY vorbisenc libvorbisenc)
find_library(VORBIS_LIBRARY NAMES vorbis libvorbis)
find_library(VORBISFILE_LIBRARY NAMES vorbisfile libvorbisfile)

find_path(LIBAIFF_INCLUDE_DIR libaiff/libaiff.h)
find_library(LIBAIFF_LIBRARY NAMES libaiff aiff)

find_path(ALAC_INCLUDE_DIR alac/ALACAudioTypes.h)
find_library(ALAC_LIBRARY NAMES libalac alac)

find_library(AAC_LIBRARY NAMES fdk-aac libfdk-aac)
find_path(AAC_INCLUDE aacdecoder_lib.h)

if(UNIX AND NOT APPLE)
  find_package(ALSA REQUIRED)
endif()

add_executable(looper src/looper_main.cc)

target_compile_definitions(looper PRIVATE -DLOOPER_VERSION="${PROJECT_VERSION}")


if(CMAKE_BUILD_TYPE MATCHES "^Debug$")
  target_compile_definitions(looper PRIVATE -DLOOPER_DEBUG=1)
endif()

if (MSVC)
target_compile_definitions(looper PRIVATE -D_CRT_SECURE_NO_WARNINGS=0)
endif()

if(Git_FOUND)
  message("Git found: ${GIT_EXECUTABLE}")
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  target_compile_definitions(looper PRIVATE -DGIT_BRANCH="${GIT_BRANCH}")
  execute_process(
    COMMAND ${GIT_EXECUTABLE} log -1 --format=%h
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  target_compile_definitions(looper PRIVATE -DGIT_COMMIT_HASH="${GIT_COMMIT_HASH}")
endif()

message(STATUS "Selected Compiler is ${CMAKE_CXX_COMPILER_ID}")

target_include_directories(
  looper
  PRIVATE
  ${OPUS_INCLUDE_DIR} ${OPUSFILE_INCLUDE_DIR}
  ${FLAC_INCLUDE_DIR} ${MPG123_INCLUDE_DIR}
  ${VORBIS_INCLUDE_DIR} ${OGG_INCLUDE_DIR}
  ${ALSA_INCLUDE_DIRS} ${LIBAIFF_INCLUDE_DIR}
  ${ALAC_INCLUDE_DIR} ${AAC_INCLUDE}
)


target_link_libraries(looper
  PRIVATE
  ${FLAC_LIBRARY}
  ${OPUSFILE_LIBRARY} ${OPUS_LIBRARY}
  ${MPG123_LIBRARY} ${VORBIS_ENC_LIBRARY}
  ${VORBISFILE_LIBRARY} ${VORBIS_LIBRARY}
  ${OGG_LIBRARY} ${LIBAIFF_LIBRARY}
  ${ALAC_LIBRARY} ${AAC_LIBRARY}
)

target_compile_features(looper PUBLIC cxx_std_17)

if (WIN32)
  target_link_libraries(looper PRIVATE  shell32 winmm)
elseif(UNIX AND NOT APPLE)
  target_link_libraries(looper PRIVATE ${ALSA_LIBRARIES} stdc++fs dl)
else()
  message( FATAL_ERROR "Not yet supported" )
endif()
