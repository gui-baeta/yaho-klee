#===------------------------------------------------------------------------===#
#
#                     The KLEE Symbolic Virtual Machine
#
# This file is distributed under the University of Illinois Open Source
# License. See LICENSE.TXT for details.
#
#===------------------------------------------------------------------------===#

file(GLOB_RECURSE check-call-path-compatibility-sources "*.cpp")

add_executable(check-call-path-compatibility
  ${check-call-path-compatibility-sources}
)

set(KLEE_LIBS
  kleaverExpr
  kleeCore
)

target_link_libraries(check-call-path-compatibility ${KLEE_LIBS})

install(TARGETS check-call-path-compatibility RUNTIME DESTINATION bin)
