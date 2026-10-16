# =============================================================================
# CMakeLists.txt
#
# WHAT IS CMAKE?
#   C++ has no single built-in build command. CMake is the most common tool
#   for describing how to build a C++ project: which source files exist, which
#   libraries they need, and which programs to produce. You describe the
#   project here once, and CMake generates the real build instructions.
#
# WHAT THIS FILE BUILDS
#   telemetry_core     a library holding all the real logic (src/ minus main)
#   telemetry_daemon   the program you run (src/main.cpp + telemetry_core)
#   unit_tests         the GoogleTest test program (tests/*.cpp + telemetry_core)
#
#   Putting the logic in a library is what lets the tests and the program
#   share exactly the same compiled code.
#
# HOW TO USE IT
#   You normally do not call CMake by hand. Run:
#       bash scripts/build_and_test.sh
#   which runs the three standard steps for you:
#       cmake -S . -B build          (configure: read this file)
#       cmake --build build          (compile)
#       ctest --test-dir build       (run the tests)
# =============================================================================

cmake_minimum_required(VERSION 3.20)

project(pi_telemetry_daemon
    VERSION 1.0.0
    DESCRIPTION "Fixed-rate sensor telemetry daemon with UDP streaming and SQLite recording"
    LANGUAGES CXX)

# ---- Language standard --------------------------------------------------------
# Use C++17, require it (do not silently fall back to something older), and
# turn off compiler-specific language extensions so the code stays portable.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# If the user did not choose a build type, use an optimised build that still
# includes debug information.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# ---- Options ------------------------------------------------------------------
# Sanitizers are compiler features that make the program check itself while
# it runs. AddressSanitizer catches memory errors (reading past the end of an
# array, using freed memory). UndefinedBehaviorSanitizer catches things like
# integer overflow. They slow the program down, so they are off by default.
# Turn on with:  bash scripts/build_and_test.sh --sanitizers
option(TELEMETRY_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

# ---- Dependencies -------------------------------------------------------------
# SQLite (the database) and the system threading library.
find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)

# ---- Compiler warnings --------------------------------------------------------
# A small "interface" target that carries the warning flags. Anything that
# links to it inherits them.
add_library(telemetry_warnings INTERFACE)
target_compile_options(telemetry_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)

if(TELEMETRY_ENABLE_SANITIZERS)
    target_compile_options(telemetry_warnings INTERFACE
        -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    target_link_options(telemetry_warnings INTERFACE -fsanitize=address,undefined)
endif()

# ---- telemetry_core library ---------------------------------------------------
add_library(telemetry_core STATIC
    src/bme280_sensor.cpp
    src/crc16.cpp
    src/daemon_config.cpp
    src/fixed_rate_timer.cpp
    src/health_monitor.cpp
    src/simulated_sensor.cpp
    src/telemetry_daemon.cpp
    src/telemetry_frame.cpp
    src/telemetry_store.cpp
    src/udp_sender.cpp)

# The real I2C bus uses a Linux-only header, so only build it on Linux. The
# compile definition lets main.cpp know whether it is available.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(telemetry_core PRIVATE src/linux_i2c_bus.cpp)
    target_compile_definitions(telemetry_core PUBLIC TELEMETRY_HAS_LINUX_I2C)
endif()

# PUBLIC means "anything that links to telemetry_core can also #include the
# headers in src/".
target_include_directories(telemetry_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(telemetry_core
    PUBLIC Threads::Threads telemetry_warnings
    PRIVATE SQLite::SQLite3)

# ---- telemetry_daemon program -------------------------------------------------
add_executable(telemetry_daemon src/main.cpp)
target_link_libraries(telemetry_daemon PRIVATE telemetry_core)

# ---- Tests --------------------------------------------------------------------
# enable_testing() turns on CTest, CMake's test runner.
enable_testing()

# GoogleTest is the unit testing framework. First look for a copy installed on
# the system (scripts/setup_ubuntu.sh installs one). If there is none,
# download it. Either way the rest of this file is identical.
find_package(GTest QUIET)
if(GTest_FOUND)
    message(STATUS "GoogleTest: using the copy installed on this system")
else()
    message(STATUS "GoogleTest: not installed, downloading it (needs git and internet access)")
    include(FetchContent)
    # Pinned to the exact commit of release v1.14.0 so every build gets the
    # same code.
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG f8d7d77c06936315286eb55f8de22cd23c188571)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

add_executable(unit_tests
    tests/test_bme280_sensor.cpp
    tests/test_crc16.cpp
    tests/test_daemon_config.cpp
    tests/test_fixed_rate_timer.cpp
    tests/test_health_monitor.cpp
    tests/test_simulated_sensor.cpp
    tests/test_telemetry_daemon.cpp
    tests/test_telemetry_frame.cpp
    tests/test_telemetry_store.cpp
    tests/test_udp_sender.cpp)
# GTest::gtest_main supplies a ready-made main() that runs every test.
target_link_libraries(unit_tests PRIVATE telemetry_core GTest::gtest_main)

# Registers each individual TEST(...) with CTest so they are listed one by one.
include(GoogleTest)
gtest_discover_tests(unit_tests)

# The end-to-end test is a Python script that launches the real daemon, listens
# for its UDP frames and checks them. It only uses Python's standard library.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME end_to_end_test
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_end_to_end.py
                $<TARGET_FILE:telemetry_daemon>)
    set_tests_properties(end_to_end_test PROPERTIES TIMEOUT 60)
else()
    message(STATUS "Python 3 not found, the end-to-end test will be skipped")
endif()
