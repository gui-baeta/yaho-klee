#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
file(GLOB_RECURSE bdd-visualizer-sources "*.cpp")

file(GLOB_RECURSE load-call-paths-sources "../load-call-paths/*.cpp")
list(FILTER load-call-paths-sources EXCLUDE REGEX ".*main\\.cpp$")

file(GLOB_RECURSE klee-util-sources "../klee-util/*.cpp")
list(FILTER klee-util-sources EXCLUDE REGEX ".*main\\.cpp$")

file(GLOB_RECURSE call-paths-to-bdd-sources "../call-paths-to-bdd/*.cpp")
list(FILTER call-paths-to-bdd-sources EXCLUDE REGEX ".*main\\.cpp$")

add_executable(bdd-visualizer
  ${bdd-visualizer-sources}
  ${load-call-paths-sources}
  ${call-paths-to-bdd-sources}
  ${klee-util-sources}
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

target_include_directories(bdd-visualizer PRIVATE ../load-call-paths ../klee-util ../call-paths-to-bdd)
target_link_libraries(bdd-visualizer ${KLEE_LIBS} nlohmann_json::nlohmann_json)

install(TARGETS bdd-visualizer RUNTIME DESTINATION bin)
