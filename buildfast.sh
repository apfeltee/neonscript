#!/bin/zsh

gottagofast=(
  # risky, but lets do it anyway.
  -Ofast
  # gentoo.jpg
  -funroll-loops
  -faggressive-loop-optimizations
  # stuff and stuff.
  -fomit-frame-pointer
  -march=native
)

set -x
g++ -Wall -Wextra ${gottagofast[@]} main.cpp -lm -ldl
 
