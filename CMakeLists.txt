cmake_minimum_required(VERSION 3.16)

project(ten_vad_ax650 VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TEN_VAD_SDK_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../../board_sdk"
    CACHE PATH "AX650 SDK directory containing include/ and lib/")

foreach(required_file
        "${TEN_VAD_SDK_ROOT}/include/ax_engine_api.h"
        "${TEN_VAD_SDK_ROOT}/include/ax_sys_api.h"
        "${TEN_VAD_SDK_ROOT}/lib/libax_engine.so"
        "${TEN_VAD_SDK_ROOT}/lib/libax_interpreter.so"
        "${TEN_VAD_SDK_ROOT}/lib/libax_sys.so")
  if(NOT EXISTS "${required_file}")
    message(FATAL_ERROR "Required AX650 SDK file not found: ${required_file}")
  endif()
endforeach()

add_library(ax_engine SHARED IMPORTED)
set_target_properties(ax_engine PROPERTIES
  IMPORTED_LOCATION "${TEN_VAD_SDK_ROOT}/lib/libax_engine.so")

add_library(ax_interpreter SHARED IMPORTED)
set_target_properties(ax_interpreter PROPERTIES
  IMPORTED_LOCATION "${TEN_VAD_SDK_ROOT}/lib/libax_interpreter.so")

add_library(ax_sys SHARED IMPORTED)
set_target_properties(ax_sys PROPERTIES
  IMPORTED_LOCATION "${TEN_VAD_SDK_ROOT}/lib/libax_sys.so")

add_library(ten_vad SHARED
  src/aed.cc
  src/biquad.cc
  src/fftw.c
  src/fscvrt.cc
  src/pitch_est.cc
  src/stft.cc
  src/ten_vad.cc
)

target_include_directories(ten_vad
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${TEN_VAD_SDK_ROOT}/include"
)

target_compile_options(ten_vad PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ten_vad PRIVATE ax_engine ax_interpreter ax_sys)

set_target_properties(ten_vad PROPERTIES
  OUTPUT_NAME ten_vad
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
  SKIP_BUILD_RPATH YES
  INSTALL_RPATH ""
)

add_executable(ten_vad_example examples/example.c)
target_compile_options(ten_vad_example PRIVATE -Wall -Wextra -Werror)
target_link_libraries(ten_vad_example PRIVATE ten_vad)
set_target_properties(ten_vad_example PROPERTIES
  BUILD_RPATH "\$ORIGIN/../aarch64-lib/lib"
  INSTALL_RPATH "\$ORIGIN/../aarch64-lib/lib"
  BUILD_WITH_INSTALL_RPATH YES
)

# CMAKE_INSTALL_PREFIX should point to plugin-ten-vad.
install(TARGETS ten_vad
  LIBRARY DESTINATION aarch64-lib/lib
)
install(FILES include/ten_vad.h
  DESTINATION aarch64-lib/include
)
install(TARGETS ten_vad_example
  RUNTIME DESTINATION example
)
