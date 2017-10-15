cmake_minimum_required(VERSION 3.9.0)
project(looper CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_path(OPUS_INCLUDE_DIR NAMES opus/opus.h)
find_library(OPUS_LIBRARY NAMES opus)

find_path(OPUSFILE_INCLUDE_DIR NAMES opus/opusfile.h)
find_library(OPUSFILE_LIBRARY NAMES opusfile)

find_path(MPG123_INCLUDE_DIR NAMES mpg123.h)
find_library(MPG123_LIBRARY  NAMES mpg123 libmpg123)

find_path(FLAC_INCLUDE_DIR FLAC/all.h)
find_library(FLAC_LIBRARY NAMES FLAC flac)

find_path(VORBIS_INCLUDE_DIR vorbis/vorbisfile.h)
find_path(OGG_INCLUDE_DIR ogg/ogg.h)

find_library(OGG_LIBRARY NAMES ogg)
find_library(VORBIS_LIBRARY NAMES vorbis)
find_library(VORBISFILE_LIBRARY NAMES vorbisfile)

if(UNIX AND NOT APPLE)
  find_package(ALSA REQUIRED)
endif()

if(CMAKE_COMPILER_IS_GNUCXX)
  add_definitions(-Wall)
endif()

if(MSVC)
  add_compile_options(/W4 -D_CRT_SECURE_NO_WARNINGS)
endif()

include_directories(
  ${OPUS_INCLUDE_DIR} ${OPUSFILE_INCLUDE_DIR}
  ${FLAC_INCLUDE_DIR} ${MPG123_INCLUDE_DIR}
  ${VORBIS_INCLUDE_DIR} ${OGG_INCLUDE_DIR}
  ${ALSA_INCLUDE_DIRS}
)


add_executable(looper looper_main.cc)

target_link_libraries(looper
  PRIVATE
  ${OPUS_LIBRARY} ${OPUSFILE_LIBRARY}
  ${FLAC_LIBRARY} ${MPG123_LIBRARY}
  ${OGG_LIBRARY} ${VORBISFILE_LIBRARY}
  ${VORBIS_LIBRARY}
)

if (WIN32)
  target_link_libraries(looper PRIVATE  shell32 winmm)
elseif(UNIX AND NOT APPLE)
  target_link_libraries(looper PRIVATE ${ALSA_LIBRARIES} stdc++fs)
else()
  message( FATAL_ERROR "Not yet supported" )
endif()
