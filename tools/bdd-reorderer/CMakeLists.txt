#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
file(GLOB_RECURSE bdd-reorderer-sources "*.cpp")

file(GLOB_RECURSE load-call-paths-sources "../load-call-paths/*.cpp")
list(FILTER load-call-paths-sources EXCLUDE REGEX ".*main\\.cpp$")

file(GLOB_RECURSE klee-util-sources "../klee-util/*.cpp")
list(FILTER klee-util-sources EXCLUDE REGEX ".*main\\.cpp$")

file(GLOB_RECURSE call-paths-to-bdd-sources "../call-paths-to-bdd/*.cpp")
list(FILTER call-paths-to-bdd-sources EXCLUDE REGEX ".*main\\.cpp$")

file(GLOB_RECURSE bdd-visualizer-sources "../bdd-visualizer/*.cpp")
list(FILTER bdd-visualizer-sources EXCLUDE REGEX ".*main\\.cpp$")

add_executable(bdd-reorderer
  ${bdd-reorderer-sources}
  ${load-call-paths-sources}
  ${call-paths-to-bdd-sources}
  ${klee-util-sources}
  ${bdd-visualizer-sources}
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

target_include_directories(bdd-reorderer PRIVATE ../load-call-paths ../call-paths-to-bdd ../klee-util ../bdd-visualizer)
target_link_libraries(bdd-reorderer ${KLEE_LIBS} nlohmann_json::nlohmann_json)

install(TARGETS bdd-reorderer RUNTIME DESTINATION bin)
