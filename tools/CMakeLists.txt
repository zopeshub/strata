# Command-line tools. All implemented; they drive YOUR engine, so each
# starts working when the milestone it depends on is done.
#
#   strata_cli          interactive shell over the public API          (M2+)
#   strata_wal_dump     print WAL records and decoded write batches      (M2)
#   strata_sst_dump     print an SSTable's entries, properties, stats    (M4)
#   strata_crash_writer / strata_crash_verify   kill -9 harness (M2 demo; see crash_test.sh)

foreach(tool strata_cli strata_wal_dump strata_sst_dump strata_crash_writer strata_crash_verify)
  add_executable(${tool} ${tool}.cc)
  target_link_libraries(${tool} PRIVATE strata)
  strata_set_warnings(${tool})
endforeach()
