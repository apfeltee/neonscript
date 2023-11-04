#!/bin/zsh
args=()
for arg in "$@"; do
  if [[ "$arg" == "-d" ]]; then
    withvalgrind=1
  else
    args+=("$arg")
  fi
done
thisdir="$(dirname "$(readlink -ef "$0")")"
if [[ "$withvalgrind" == 1 ]]; then
  exec valgrind node "$thisdir/etc/nodeshim.js" "${args[@]}"
else
  exec node "$thisdir/etc/nodeshim.js" "${args[@]}"
fi
