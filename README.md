
Neon is bare-necessities Javascript-alike language.  
Intended for embedding, but right now, the focus is on ironing out potential bugs, to provide the most stable, no-nonsense codebase possible.  
API is quite straight-forward too: take a look at the `main` function in `main.c`.

#features:
  - historically based on Robert Nystrom's 'Lox' language. But wildly deviated since then.
  - small memory footprint, thanks to an memory pool allocator based on dlmalloc
  - supports a fairly decent coverage of Javascript, but does ***not*** try to be Javascript. Neon is not Javascript, it only looks like it.
  - decently fast, even without compiler optimizations (`-O0`), and quite fast with heavy optimizations (`-O3`, or even GCC's `-O5`)
  - tested with gcc, clang, tcc, visual studio
  - should compile without warnings of any kind! although some compilers are pettier than others.

*did you know?*
Neon does not use setjmp for exceptions; which means the entire codebase can be compiled into pure CLR code:

    mkdir clrtest
    cat > all.cpp <<__eos
        #include "../main.c"
        #include "../allocator.c"
        #include "../lino.c"
        #include "../mem.c"
        #include "../optparse.c"
        #include "../os.c"
        #include "../strbuf.c"
    __eos
    cl.exe -clr:pure all.cpp

the resulting executable `all.exe` is pure CLR.  
While naturally the code contains plenty of `unsafe` regions, this is evidently because of pointers.