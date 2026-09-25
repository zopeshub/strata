include(FetchContent)

# Pinned releases, downloaded as tarballs (faster than git clones).
if(STRATA_BUILD_TESTS)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

if(STRATA_BUILD_BENCH)
  FetchContent_Declare(benchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(benchmark)
endif()

# io_uring (Linux only) via liburing.
set(STRATA_HAVE_IO_URING OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT STRATA_IO_URING STREQUAL "OFF")
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBURING IMPORTED_TARGET liburing)
  endif()
  if(LIBURING_FOUND)
    set(STRATA_HAVE_IO_URING ON)
  elseif(STRATA_IO_URING STREQUAL "ON")
    message(FATAL_ERROR "STRATA_IO_URING=ON but liburing was not found (apt install liburing-dev)")
  endif()
elseif(STRATA_IO_URING STREQUAL "ON")
  message(FATAL_ERROR "io_uring is Linux-only; use the Lima VM (see curriculum/00-setup.md)")
endif()
