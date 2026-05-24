cmake_minimum_required(VERSION 3.10)
project(spectra C)
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ── Compiler flags ──────────────────────────────────────────────────────────
if(MSVC)
    add_compile_options(/W3 /O2)
else()
    add_compile_options(-Wall -Wextra -O2)
endif()

# ── Source lists ─────────────────────────────────────────────────────────────

set(RUNTIME_SOURCES
    runtime/specton.c
    runtime/tensor.c
    runtime/memory.c
    runtime/neural.c
)

set(COMPILER_SOURCES
    src/ast.c
    src/lexer.c
    src/parser.c
    src/value.c
    src/env.c
    src/interpreter.c
    src/builtins.c
    src/modules.c
    src/repl.c
    src/simulator.c
    src/main.c
)

# ── Executable ───────────────────────────────────────────────────────────────

add_executable(spectra ${RUNTIME_SOURCES} ${COMPILER_SOURCES})

target_include_directories(spectra PRIVATE
    src
    runtime
)

# ── Link math library ────────────────────────────────────────────────────────
# On Windows with MSVC, math is part of the CRT — no extra flag needed.
if(NOT MSVC)
    target_link_libraries(spectra m)
endif()

# ── pthreads (optional — used by future concurrency features) ─────────────────
if(WIN32)
    find_library(PTHREAD_LIB NAMES pthreadGC2 pthreadVC2 pthread QUIET)
    if(PTHREAD_LIB)
        target_link_libraries(spectra ${PTHREAD_LIB})
    endif()
else()
    target_link_libraries(spectra pthread)
endif()

# ── Installation ─────────────────────────────────────────────────────────────
install(TARGETS spectra DESTINATION bin)
