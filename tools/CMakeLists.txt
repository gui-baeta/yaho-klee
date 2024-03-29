# ===------------------------------------------------------------------------===#
#
# The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
# ===------------------------------------------------------------------------===#
add_subdirectory(gen-random-bout)
add_subdirectory(kleaver)
add_subdirectory(klee)
add_subdirectory(klee-replay)
add_subdirectory(klee-stats)
add_subdirectory(ktest-tool)
add_subdirectory(ktest-dehavoc)
add_subdirectory(stitch-perf-contract)
add_subdirectory(check-call-path-compatibility)
add_subdirectory(klee-util)
add_subdirectory(load-call-paths)
add_subdirectory(analyze-call-paths)
add_subdirectory(call-paths-to-bdd)
add_subdirectory(bdd-to-c)
add_subdirectory(bdd-reorderer)
add_subdirectory(synapse)
add_subdirectory(packet-modification-detector)
add_subdirectory(clone)
add_subdirectory(bdd-emulator)
