#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
file(GLOB_RECURSE bdd-emulator-sources "*.cpp")

file(GLOB_RECURSE load-call-paths-sources "../load-call-paths*.cpp")
list(FILTER load-call-paths-sources EXCLUDE REGEX ".*main\\.cpp$")

file(GLOB_RECURSE call-paths-to-bdd-sources "../call-paths-to-bdd/*.cpp")
list(FILTER call-paths-to-bdd-sources EXCLUDE REGEX ".*main\\.cpp$")

file(GLOB_RECURSE klee-util-sources "../klee-util/*.cpp")
list(FILTER klee-util-sources EXCLUDE REGEX ".*main\\.cpp$")

add_executable(bdd-emulator
  ${bdd-emulator-sources}
  ${load-call-paths-sources}
  ${call-paths-to-bdd-sources}
  ${klee-util-sources}
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

find_package(PCAP REQUIRED)

target_include_directories(bdd-emulator PRIVATE ../load-call-paths ../call-paths-to-bdd ../klee-util)
target_link_libraries(bdd-emulator ${KLEE_LIBS} ${PCAP_LIBRARY})

install(TARGETS bdd-emulator RUNTIME DESTINATION bin)
