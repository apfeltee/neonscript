#!/bin/zsh

file="$1"
prefix="$2"

if [[ "$file" ]] && [[ "prefix" ]]; then
  cproto -si "$file" | grep static | grep -Pv "\\b$prefix"
else
  echo "usage: <file> <prefix>"
fi