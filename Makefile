
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

## has a tendency to report false positives!
#unflags += -pedantic


allunflags = $(unflags)
#allunflags = $(unflags) 

WFLAGS = -Wall -Wextra -Wshadow -Wpointer-arith -Wuninitialized -Winit-self -Wmaybe-uninitialized $(allunflags)
#WFLAGS = -Wall -Wextra -Wshadow
#WFLAGS += -ansi -pedantic
#WFLAGS = -w


OPTFLAGS = -O0
#OPTFLAGS = -O5 -funroll-loops -flto -ffast-math 

## empty by default
EXTRAFLAGS =
## might be needed for -fsanitize* to work properly, but that's just a theory.
#EXTRAFLAGS =  -mno-red-zone -fno-omit-frame-pointer

#EXTRAFLAGS += -fno-stack-protector

#*** WARNING ***
# causes quite a steep use of memory!
#EXTRAFLAGS += -fsanitize=undefined

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

objfiles_all = $(srcfiles_all:.c=.o)
depfiles_all = $(objfiles_all:.o=.d)
protofile = prot.inc

# janky mess
havecproto = 0
ifeq (, $(shell which cproto))
havecproto = 0
endif

all: $(protofile) $(target)

## dear god what a kludge
ifeq (1, $(havecproto))
$(protofile): $(srcfiles_all) $(headerfiles_all)
	echo > $(protofile)
	cproto $(srcfiles_all) $(headerfiles_all) 2>/dev/null | perl -pe 's/\b_Bool\b/bool/g' | grep -vP __inline > $(protofile)_tmp
	mv $(protofile)_tmp $(protofile)
else
$(protofile): $(srcfiles_all)
endif

$(target): $(objfiles_all)
	@echo "LINK $@"
	@$(CC) -o $@ $^ $(LDFLAGS)

-include $(depfiles_all)

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.c
	@echo "DEP $@ -> $<"
	@$(CC) $(CFLAGS) $< -MM -MT $(@:.d=.o) -MF $@

%.o: %.c
	@echo "CC $< -> $@"
	@$(CC) $(CFLAGS) -c $(DBGFLAGS) -o $@ $<

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
