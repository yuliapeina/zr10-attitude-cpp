# zr10-attitude 0.3.8 — плагін R2D2 для гімбала SIYI ZR10 (OSD кутів гімбала).
# Збирає ОДНУ програму: zr10-attitude (статичний бінарник для камери).
#
# Залежність: заголовки MAVLink c_library_v2, commit
#   6a7760569ce6d46c56f93163703543d4345e940a (діалект ardupilotmega).
# Де CMake їх бере (по черзі):
#   1) -DMAVLINK_DIR=/шлях/до/c_library_v2 — якщо задано;
#   2) third_party/mavlink поруч із цим файлом — якщо є (збірка без інтернету);
#   3) інакше завантажує з GitHub (FetchContent).
#
# Приклад крос-збірки для SSC30KQ (armv7l, Cortex-A7, hard-float):
#   cmake -B build -DCMAKE_BUILD_TYPE=MinSizeRel \
#         -DCMAKE_TOOLCHAIN_FILE=<ваш toolchain armv7 hard-float>
#   cmake --build build
cmake_minimum_required(VERSION 3.16)
project(zr10_attitude VERSION 0.3.8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ZR10_STATIC "Статичне лінкування (для камери)" ON)
set(MAVLINK_DIR "" CACHE PATH "Готові заголовки MAVLink c_library_v2 (порожньо — завантажити)")
set(MAVLINK_COMMIT 6a7760569ce6d46c56f93163703543d4345e940a)

if(MAVLINK_DIR)
  set(ZR10_MAVLINK_INCLUDE ${MAVLINK_DIR})
elseif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mavlink/ardupilotmega/mavlink.h)
  set(ZR10_MAVLINK_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mavlink)
  message(STATUS "MAVLink: third_party/mavlink (вбудовані заголовки)")
else()
  include(FetchContent)
  FetchContent_Declare(mavlink_c_library_v2
    GIT_REPOSITORY https://github.com/mavlink/c_library_v2.git
    GIT_TAG ${MAVLINK_COMMIT})
  FetchContent_GetProperties(mavlink_c_library_v2)
  if(NOT mavlink_c_library_v2_POPULATED)
    FetchContent_Populate(mavlink_c_library_v2)
  endif()
  set(ZR10_MAVLINK_INCLUDE ${mavlink_c_library_v2_SOURCE_DIR})
endif()

add_executable(zr10-attitude
  src/main.cpp
  src/attitude.cpp
  src/config.cpp
  src/draw.cpp
  src/mav.cpp
  src/osd.cpp
  src/siyi.cpp)
target_include_directories(zr10-attitude PRIVATE include)
# -isystem: попередження з коду MAVLink не показуємо.
target_include_directories(zr10-attitude SYSTEM PRIVATE ${ZR10_MAVLINK_INCLUDE})
target_compile_options(zr10-attitude PRIVATE -Wall -Wextra)
if(ZR10_STATIC)
  target_link_options(zr10-attitude PRIVATE -static)
endif()
if(CMAKE_BUILD_TYPE MATCHES "Release|MinSizeRel")
  target_link_options(zr10-attitude PRIVATE -s)
endif()
if(WIN32)
  target_link_libraries(zr10-attitude PRIVATE ws2_32)
endif()
