cmake_minimum_required(VERSION 3.24)

# -------------------------------
# Toolchain: clang++ + libc++
# (Only honored on a *fresh* build dir)
# -------------------------------
if(NOT DEFINED CMAKE_C_COMPILER)
  set(CMAKE_C_COMPILER clang)
endif()
if(NOT DEFINED CMAKE_CXX_COMPILER)
  set(CMAKE_CXX_COMPILER clang++)
endif()

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
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  add_compile_options(-stdlib=libstdc++)
  add_link_options(-stdlib=libstdc++)
  # Linux needs explicit c++abi and pthread
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_link_options(-lc++abi -pthread)
  endif()
endif()

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

# -------------------------------
# Your target
# -------------------------------
add_executable(trying_coroutines
  src/main.cpp
)

# Warnings (tweak as you like)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  target_compile_options(trying_coroutines PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

# Link deps
# The libcoro project defines a target named "libcoro".
target_link_libraries(trying_coroutines
  PRIVATE
    libcoro
    Threads::Threads
)

