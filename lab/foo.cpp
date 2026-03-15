
#include <cstdio>
#include <iostream>

auto foo()
{
    struct {
        int a;
        int b;
    } things;
    things.a = 42;
    things.b = 1337;
    return things;
}


int main()
{
    auto rt = foo();
    printf("rt.a=%d, rt.b=%d\n", rt.a, rt.b);
}

