#!/bin/zsh

cppcheck -D__CPPCHECK__ --enable=all --force --check-level=exhaustive main.cpp |& tee result.txt