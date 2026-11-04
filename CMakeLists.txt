cmake_minimum_required(VERSION 3.14)

project(Arduino-TimeServices LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TS_BUILD_EXAMPLES "Build the desktop examples" ON)
option(TS_BUILD_TESTS "Build unit tests" ON)

# Override any of these with -DFETCHCONTENT_SOURCE_DIR_<NAME>=/path/to/local
# to build against local checkouts instead of fetching them from GitHub.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Only our own examples/tests are wanted.
set(TMD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TMD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TGPU_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TINYNTP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(TINYNTP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  tinymaterialdesign
  GIT_REPOSITORY https://github.com/pschatzmann/TinyMaterialDesign.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(tinymaterialdesign)

# The Arduino API on the desktop (millis(), WiFiUDP, ...). No HTTPS: NTP is
# UDP, so the emulator is built without wolfSSL (much faster).
set(USE_HTTPS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  arduino_emulator
  GIT_REPOSITORY https://github.com/pschatzmann/Arduino-Emulator.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(arduino_emulator)

FetchContent_Declare(
  tinyntp
  GIT_REPOSITORY https://github.com/pschatzmann/TinyNTP.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(tinyntp)

# Header-only interface library
add_library(Arduino-TimeServices INTERFACE)
target_include_directories(Arduino-TimeServices INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(Arduino-TimeServices INTERFACE TinyMaterialDesign TinyNTP arduino_emulator)
# The Arduino API of the emulator is used when ARDUINO is defined
target_compile_definitions(Arduino-TimeServices INTERFACE ARDUINO)

if(TS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(TS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
