#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
file(GLOB_RECURSE bdd-to-c-sources "*.cpp")
list(FILTER bdd-to-c-sources EXCLUDE REGEX ".*\\.template\\..*")

file(GLOB_RECURSE load-call-paths-sources "../load-call-paths/*.cpp")
list(FILTER load-call-paths-sources EXCLUDE REGEX ".*main\\.cpp$")

file(GLOB_RECURSE klee-util-sources "../klee-util/*.cpp")
list(FILTER klee-util-sources EXCLUDE REGEX ".*main\\.cpp$")

file(GLOB_RECURSE call-paths-to-bdd-sources "../call-paths-to-bdd/*.cpp")
list(FILTER call-paths-to-bdd-sources EXCLUDE REGEX ".*main\\.cpp$")

add_executable(bdd-to-c
  ${bdd-to-c-sources}
  ${call-paths-to-bdd-sources}
  ${load-call-paths-sources}
  ${klee-util-sources}
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

include(${CMAKE_SOURCE_DIR}/cmake/find_json.cmake)

target_include_directories(bdd-to-c PRIVATE ../load-call-paths ../call-paths-to-bdd ../klee-util)
target_link_libraries(bdd-to-c ${KLEE_LIBS})

install(TARGETS bdd-to-c RUNTIME DESTINATION bin)
