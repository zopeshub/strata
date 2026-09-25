include(GoogleTest)

add_library(strata_test_support STATIC
  support/mem_fs.cc
  support/test_util.cc)
target_include_directories(strata_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(strata_test_support PUBLIC strata GTest::gtest GTest::gmock)
strata_set_warnings(strata_test_support)

# strata_test(<label> <name>): builds tests/<label>/<name>.cc, labels every
# discovered test case with <label> so `ctest -L m2` runs one milestone.
function(strata_test label name)
  add_executable(${name} ${label}/${name}.cc)
  target_link_libraries(${name} PRIVATE strata_test_support GTest::gtest_main)
  strata_set_warnings(${name})
  gtest_discover_tests(${name}
    NO_PRETTY_VALUES
    PROPERTIES LABELS "${label}" TIMEOUT 600
    DISCOVERY_TIMEOUT 60)
endfunction()

# Test harness self-tests (pass from day one).
strata_test(harness mem_fs_test)

# M1: foundations
strata_test(m1 coding_test)
strata_test(m1 crc32c_test)
strata_test(m1 file_descriptor_test)
strata_test(m1 posix_fs_test)

# M2: durable KV (WAL + simple memtable + recovery)
strata_test(m2 dbformat_test)
strata_test(m2 write_batch_test)
strata_test(m2 filename_test)
strata_test(m2 log_test)
strata_test(m2 memtable_test)
strata_test(m2 db_basic_test)
strata_test(m2 db_recovery_test)

# M3: arena + concurrent skiplist memtable
strata_test(m3 arena_test)
strata_test(m3 skiplist_test)
strata_test(m3 concurrent_memtable_test)

# M4: SSTable format
strata_test(m4 format_test)
strata_test(m4 block_test)
strata_test(m4 bloom_test)
strata_test(m4 table_test)

# M5: flush, read path across levels, iterators, snapshots, ranges
strata_test(m5 merging_iterator_test)
strata_test(m5 db_flush_test)
strata_test(m5 db_iterator_test)
strata_test(m5 range_test)
strata_test(m5 db_model_m5_test)

# M6: MANIFEST, versions, leveled compaction, crash safety
strata_test(m6 version_edit_test)
strata_test(m6 compaction_test)
strata_test(m6 db_model_m6_test)
strata_test(m6 db_crash_test)

# M7: concurrency and failure handling
strata_test(m7 group_commit_test)
strata_test(m7 db_concurrency_test)
strata_test(m7 db_error_test)

# M8: coroutines + async I/O
strata_test(m8 task_test)
strata_test(m8 io_context_test)
