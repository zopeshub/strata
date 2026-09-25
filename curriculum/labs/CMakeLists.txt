# Curriculum labs: small standalone programs (libc + C++ only, no strata).
# Each has LAB_TODO markers where you write the interesting part.
#
#   ./build/debug/curriculum/labs/lab01_atomic_rename
#
# Linux-only labs are skipped on macOS; run them in the Lima VM.

function(strata_lab name)
  add_executable(${name} ${name}.cc)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE Threads::Threads)
  strata_set_warnings(${name})
endfunction()

strata_lab(lab01_atomic_rename)
strata_lab(lab02_durable_create)
strata_lab(lab03_mmap_faults)
strata_lab(lab04_page_cache)
strata_lab(lab05_io_path_probe)
strata_lab(lab07_fsync_latency)
strata_lab(lab08_group_commit)

if(STRATA_HAVE_IO_URING)
  strata_lab(lab06_uring_read)
  target_link_libraries(lab06_uring_read PRIVATE PkgConfig::LIBURING)
endif()
