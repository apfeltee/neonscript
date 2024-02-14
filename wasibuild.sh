#!/bin/zsh

sources=(
  "main.cpp"
)

cflags=(
  -w
  -D_WASI_EMULATED_SIGNAL
)

set -x
clang++ --target=wasm32-wasi "${sources[@]}" "${cflags[@]}" -static -v -nodefaultlibs -lc -lm "$@"


