#!/bin/zsh

incdirs=(
 /usr/include/c++/14
 /usr/include/x86_64-linux-gnu/c++/14
 /usr/include/c++/14/backward
 /usr/lib/gcc/x86_64-linux-gnu/14/include
 /usr/local/include
 /usr/include/x86_64-linux-gnu
 /usr/include
)

incdirs=()

defflags=(
  -D__CHAR_BIT__=1
  -D__CPPCHECK__
)

incflags=()

for incdir in "${incdirs[@]}"; do
  incflags+=(-I"${incdir}")
done



cppcheck "${incflags[@]}" "${defflags[@]}" --enable=all --force --check-level=exhaustive main.cpp |& tee result.txt