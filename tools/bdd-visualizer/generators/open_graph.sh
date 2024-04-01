#!/bin/bash

set -e

if [[ $# -eq 0 ]] ; then
  exit 1
fi

function open {
  xdot $1 &> /dev/null 2>&1 &
}

for f in "${@}"; do
  open "$f"
done

exit 0
