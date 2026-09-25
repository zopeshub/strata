# Compiler warnings and sanitizer plumbing.
#
# Warnings are applied per-target (our code only) so third-party code
# (googletest, benchmark) doesn't drown the signal.

function(strata_set_warnings target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(${target} PRIVATE
      -Wall
      -Wextra
      -Wpedantic
      -Wshadow
      -Wnon-virtual-dtor
      -Wold-style-cast
      -Wcast-align
      -Woverloaded-virtual
      -Wnull-dereference
      -Wimplicit-fallthrough
      -Wformat=2)
    if(STRATA_STRICT)
      target_compile_options(${target} PRIVATE -Wconversion -Wsign-conversion)
    endif()
    if(STRATA_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

function(strata_configure_sanitizers)
  if(NOT STRATA_SANITIZE)
    return()
  endif()
  if("thread" IN_LIST STRATA_SANITIZE AND "address" IN_LIST STRATA_SANITIZE)
    message(FATAL_ERROR "ThreadSanitizer and AddressSanitizer cannot be combined")
  endif()
  list(JOIN STRATA_SANITIZE "," _san)
  # Applied globally on purpose: sanitizers need the whole program
  # (including gtest) instrumented to avoid false positives.
  add_compile_options(-fsanitize=${_san} -fno-omit-frame-pointer -fno-sanitize-recover=all)
  add_link_options(-fsanitize=${_san})
  if("address" IN_LIST STRATA_SANITIZE)
    add_compile_definitions(STRATA_ASAN=1)
  endif()
  if("thread" IN_LIST STRATA_SANITIZE)
    add_compile_definitions(STRATA_TSAN=1)
  endif()
endfunction()
