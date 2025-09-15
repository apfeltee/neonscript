
INCFLAGS =

unflags = \
	-Wunused \
	-Wunused-but-set-parameter \
	-Wunused-but-set-variable \
	-Wunused-const-variable \
	-Wunused-const-variable \
	-Wno-unused-function \
	-Wunused-label \
	-Wunused-local-typedefs \
	-Wunused-macros \
	-Wunused-parameter \
	-Wunused-result \
	-Wunused-value \
	-Wunused-variable \
	-Wvla \

unflags += -Wjump-misses-init

unflags =
## has a tendency to report false positives!
#unflags += -pedantic


allunflags = $(unflags)
#allunflags = $(unflags) 

WFLAGS = -Wall -Wextra -Wshadow -Wpointer-arith -Wuninitialized -Winit-self -Wmaybe-uninitialized $(allunflags)
#WFLAGS = -Wall -Wextra -Wshadow
#WFLAGS += -ansi -pedantic
#WFLAGS = -w


OPTFLAGS = -O5
#OPTFLAGS = -O5 -funroll-loops -flto -ffast-math 

## empty by default
EXTRAFLAGS =
## might be needed for -fsanitize* to work properly, but that's just a theory.
#EXTRAFLAGS =  -mno-red-zone -fno-omit-frame-pointer
#EXTRAFLAGS += -fsanitize=memory
#EXTRAFLAGS += -fsanitize=address -fstack-protector-all -ftrapv

### WARNING: can be quite verbose! prints unused sections, giving a better clue which functions can be removed.
#EXTRAFLAGS += -fdata-sections -ffunction-sections -Wl,--gc-sections -Wl,--print-gc-sections

#CC = clang++ -gdwarf-4 $(WFLAGS) $(EXTRAFLAGS)
#CC = g++ $(WFLAGS) $(EXTRAFLAGS)
CC = gcc $(WFLAGS) $(EXTRAFLAGS)
#CC = clang -gdwarf-4 $(WFLAGS) $(EXTRAFLAGS)
#CC = tcc $(WFLAGS) $(EXTRAFLAGS)

#CFLAGS = $(INCFLAGS) -Ofast -march=native -flto -ffast-math -funroll-loops
CFLAGS = $(INCFLAGS) $(OPTFLAGS) -g3 -ggdb3
LDFLAGS = -ldl -lm
target = run

srcfiles_all = $(wildcard *.c)
headerfiles_all = $(wildcard *.h)

asmfiles_all = $(srcfiles_all:.c=.s)
objfiles_all = $(srcfiles_all:.c=.o)
depfiles_all = $(objfiles_all:.o=.d)

.PHONY: all
all: $(target)

$(objfiles_all): $(asmfiles_all)

$(target): $(objfiles_all)
	$(CC) -o $(target) $(objfiles_all) $(LDFLAGS)

-include $(depfiles_all)


# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.c
	$(CC) $(CFLAGS) $< -MM -MT $(@:.d=.o) -MF $@

%.s: %.c
	$(CC) $(CFLAGS) -S $(DBGFLAGS) -o $@ $<


%.o: %.s
	$(CC) $(CFLAGS) -c $(DBGFLAGS) -o $@ $<


.PHONY: clean
clean:
	rm -f $(objfiles_all) $(target) *.exe *.ilk *.obj *.pdb

.PHONY: cleandep
cleandep:
	rm -f $(depfiles_all)

.PHONY: distclean
distclean: cleandep clean

.PHONY: rebuild
rebuild: clean cleandep $(target)
