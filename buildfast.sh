#!/bin/zsh

gottagofast=(
  # risky, but lets do it anyway.
  -Ofast
  # gentoo.jpg
  -funroll-loops
  -faggressive-loop-optimizations
  # stuff and stuff.
  -fomit-frame-pointer
  #-march=native
)

set -x
g++ -g3 -ggdb3 -Wall -Wextra ${gottagofast[@]} main.cpp -lm -ldl
 
