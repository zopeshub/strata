# Benchmarks.
#
#   strata_bench   end-to-end, db_bench-style workloads (implemented; drives
#                  YOUR engine through the public API)
#   bench_*        Google Benchmark micro-benchmarks of individual modules
#
# Always measure with the release preset:  cmake --build --preset release
# (debug builds are 5-20x slower and the numbers mean nothing).

add_executable(strata_bench strata_bench.cc histogram.cc)
target_link_libraries(strata_bench PRIVATE strata)
strata_set_warnings(strata_bench)

function(strata_micro_bench name)
  add_executable(${name} ${name}.cc)
  target_link_libraries(${name} PRIVATE strata benchmark::benchmark)
  strata_set_warnings(${name})
endfunction()

strata_micro_bench(bench_coding)     # M1: varint / fixed encode+decode
strata_micro_bench(bench_crc32c)     # M1: table-driven vs (stretch) hardware CRC
strata_micro_bench(bench_fsync)      # M1: fsync vs fdatasync vs F_FULLFSYNC, by write size
strata_micro_bench(bench_memtable)   # M2/M3: std::map memtable vs arena skiplist; concurrent reads
strata_micro_bench(bench_bloom)      # M4: build/probe cost, measured false-positive rate
strata_micro_bench(bench_block)      # M4: block build, seek, scan vs restart interval
strata_micro_bench(bench_merge)      # M5: merging iterator vs number of children
strata_micro_bench(bench_async_read) # M8: blocking vs io_uring random reads, queue depth sweep
