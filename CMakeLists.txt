cmake_minimum_required(VERSION 3.20)
project(WinModPlayer LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT -> single, dependency-free .exe
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# libopenmpt (same 0.8.9 sources as the Android CarModPlayer), built statically
# ---------------------------------------------------------------------------
set(OMPT ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libopenmpt)

file(GLOB_RECURSE OPENMPT_SOURCES
  ${OMPT}/common/*.cpp
  ${OMPT}/soundlib/*.cpp
  ${OMPT}/sounddsp/*.cpp
)
list(APPEND OPENMPT_SOURCES
  ${OMPT}/libopenmpt/libopenmpt_c.cpp
  ${OMPT}/libopenmpt/libopenmpt_cxx.cpp
  ${OMPT}/libopenmpt/libopenmpt_impl.cpp
  ${OMPT}/libopenmpt/libopenmpt_ext_impl.cpp
)

add_library(openmpt STATIC ${OPENMPT_SOURCES})
target_include_directories(openmpt
  PUBLIC  ${OMPT}
  PRIVATE ${OMPT}/src ${OMPT}/common ${OMPT}/soundlib
)
target_compile_definitions(openmpt
  PUBLIC  LIBOPENMPT_NO_DEPRECATE
  PRIVATE LIBOPENMPT_BUILD NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE
)
target_compile_options(openmpt PRIVATE /W0 /O2 /bigobj /utf-8 /permissive- /Zc:__cplusplus /EHsc)
target_link_libraries(openmpt PUBLIC rpcrt4 ole32 winmm)

# ---------------------------------------------------------------------------
# The player
# ---------------------------------------------------------------------------
add_executable(WinModPlayer WIN32
  src/main.cpp
  src/audio.cpp
  src/playlist.cpp
  src/app.rc
)
target_include_directories(WinModPlayer PRIVATE src)
target_compile_definitions(WinModPlayer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0A00)
target_compile_options(WinModPlayer PRIVATE /W3 /O2 /utf-8 /permissive- /EHsc)
target_link_options(WinModPlayer PRIVATE /MANIFEST:NO /OPT:REF /OPT:ICF)
target_link_libraries(WinModPlayer PRIVATE
  openmpt ole32 uuid shell32 shlwapi comctl32 dwmapi uxtheme gdi32 user32 avrt msimg32)
