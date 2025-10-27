cmake_minimum_required(VERSION 3.24)

# -------------------------------
# Toolchain: clang++ + libc++
# (Only honored on a *fresh* build dir)
# -------------------------------
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

project(trying_coroutines LANGUAGES CXX)

# -------------------------------
# Language + defaults
# -------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to Debug for single-config generators
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# -------------------------------
# libc++ flags for Clang
# -------------------------------
add_compile_options(-stdlib=libstdc++)
add_link_options(-stdlib=libstdc++)
add_link_options(-lc++abi -pthread)

# -------------------------------
# Dependencies (FetchContent): libcoro v0.15.0
# -------------------------------
include(FetchContent)

# Fetch libcoro source
FetchContent_Declare(
  libcoro
  GIT_REPOSITORY https://github.com/jbaldwin/libcoro.git
  GIT_TAG        v0.15.0
)
FetchContent_MakeAvailable(libcoro)

# Threads (ensures -pthread etc. where needed)
find_package(Threads REQUIRED)

# ---------------------------------
# Shared library: common
# ---------------------------------
# Put your shared headers under ${CMAKE_SOURCE_DIR}/common/*.hpp
# and sources under ${CMAKE_SOURCE_DIR}/common/*.cpp
file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/common/*.cpp"
)

add_library(common STATIC ${COMMON_SOURCES})

# Make "common/" visible to anything that links against this target
target_include_directories(common PUBLIC
  "${CMAKE_SOURCE_DIR}"        # so includes like "common/foo.hpp" work
)
# Alternatively:
# target_include_directories(common PUBLIC "${CMAKE_SOURCE_DIR}/common")

target_compile_options(common PRIVATE -Wall -Wextra -Wpedantic -Wconversion)

# -------------------------------
# Targets
# -------------------------------
add_executable(sequential ./sequential/main.cpp)
target_compile_options(sequential PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
target_link_libraries(sequential PRIVATE Threads::Threads common)

add_executable(async ./async/main.cpp)
target_compile_options(async PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
target_link_libraries(async PRIVATE Threads::Threads common)

add_executable(coro ./coro/main.cpp)
target_compile_options(coro PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
target_link_libraries(coro PRIVATE libcoro Threads::Threads common)
