#!/bin/zsh

exe="$1"
shift
wasmtime --dir . "$exe" "$@"


