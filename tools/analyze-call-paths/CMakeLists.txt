#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#
file(GLOB_RECURSE analyze-call-paths-sources
  "*.cpp"
)

file(GLOB_RECURSE load-call-paths-sources
  "../load-call-paths/*.cpp"
)

file(GLOB_RECURSE klee-util-sources
  "../klee-util/*.cpp"
)

list(FILTER load-call-paths-sources EXCLUDE REGEX ".*main\\.cpp$")
list(FILTER klee-util-sources EXCLUDE REGEX ".*main\\.cpp$")

add_executable(analyze-call-paths
  ${analyze-call-paths-sources}
  ${load-call-paths-sources}
  ${klee-util-sources}
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

target_include_directories(analyze-call-paths PRIVATE ../klee-util ../load-call-paths)
target_link_libraries(analyze-call-paths ${KLEE_LIBS})

install(TARGETS analyze-call-paths RUNTIME DESTINATION bin)
