cmake_minimum_required(VERSION 3.25)

project(strata
  VERSION 0.1.0
  DESCRIPTION "An LSM-tree storage engine in modern C++ (learning + showcase project)"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(STRATA_BUILD_TESTS  "Build the test suite"                ON)
option(STRATA_BUILD_BENCH  "Build benchmarks"                    ON)
option(STRATA_BUILD_TOOLS  "Build CLI / dump / crash tools"      ON)
option(STRATA_BUILD_LABS   "Build curriculum lab programs"       ON)
option(STRATA_WERROR       "Treat warnings as errors"            OFF)
option(STRATA_STRICT       "Extra warnings: -Wconversion etc."   OFF)
set(STRATA_SANITIZE "" CACHE STRING
    "Sanitizers to enable: 'address;undefined', 'thread', or empty")
set(STRATA_IO_URING "AUTO" CACHE STRING "io_uring backend: AUTO, ON, OFF (Linux only)")
set_property(CACHE STRATA_IO_URING PROPERTY STRINGS AUTO ON OFF)

include(cmake/StrataOptions.cmake)
strata_configure_sanitizers()

include(cmake/StrataDependencies.cmake)

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_library(strata
  src/util/status.cc
  src/util/todo.cc
  src/util/coding.cc
  src/util/crc32c.cc
  src/util/hash.cc
  src/util/arena.cc
  src/fs/posix_fs.cc
  src/fs/file_descriptor.cc
  src/db/dbformat.cc
  src/db/write_batch.cc
  src/db/memtable.cc
  src/db/log_writer.cc
  src/db/log_reader.cc
  src/db/filename.cc
  src/db/version_edit.cc
  src/db/version_set.cc
  src/db/compaction.cc
  src/db/table_cache.cc
  src/db/db_iter.cc
  src/db/db_impl.cc
  src/table/format.cc
  src/table/block_builder.cc
  src/table/block.cc
  src/table/bloom.cc
  src/table/table_builder.cc
  src/table/table.cc
  src/table/merging_iterator.cc
  src/table/iterator.cc
  src/io/io_context.cc
)

target_include_directories(strata
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src)

find_package(Threads REQUIRED)
target_link_libraries(strata PUBLIC Threads::Threads)
strata_set_warnings(strata)

if(STRATA_HAVE_IO_URING)
  target_sources(strata PRIVATE src/io/io_uring_context.cc)
  target_compile_definitions(strata PUBLIC STRATA_HAVE_IO_URING=1)
  target_link_libraries(strata PUBLIC PkgConfig::LIBURING)
endif()

add_library(strata::strata ALIAS strata)

# ---------------------------------------------------------------------------
# Subprojects
# ---------------------------------------------------------------------------
if(STRATA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(STRATA_BUILD_BENCH)
  add_subdirectory(bench)
endif()

if(STRATA_BUILD_TOOLS)
  add_subdirectory(tools)
endif()

if(STRATA_BUILD_LABS)
  add_subdirectory(curriculum/labs)
endif()

message(STATUS "strata: build type      = ${CMAKE_BUILD_TYPE}")
message(STATUS "strata: sanitizers      = '${STRATA_SANITIZE}'")
message(STATUS "strata: io_uring        = ${STRATA_HAVE_IO_URING}")
