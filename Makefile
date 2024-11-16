
INCFLAGS =

unflags = \
	-Wunused \
	-Wunused-but-set-parameter \
	-Wunused-but-set-variable \
	-Wunused-const-variable \
	-Wunused-const-variable \
	-Wunused-function \
	-Wunused-label \
	-Wunused-local-typedefs \
	-Wunused-macros \
	-Wunused-parameter \
	-Wunused-result \
	-Wunused-value \
	-Wunused-variable


WFLAGS = -Wall -Wextra -Wshadow -Wpointer-arith $(unflags)
#WFLAGS = -Wall -Wextra -Wshadow
#WFLAGS += -ansi -pedantic
#WFLAGS = -w


OPTFLAGS = -O0
#OPTFLAGS = -O5 -funroll-loops -march=native -flto -ffast-math 

#EXTRAFLAGS = -fsanitize=address -fsanitize=undefined -fstack-protector-all -ftrapv

#CC = gcc $(WFLAGS) $(EXTRAFLAGS)
CC = clang -gdwarf-4 $(WFLAGS) $(EXTRAFLAGS)

#CFLAGS = $(INCFLAGS) -Ofast -march=native -flto -ffast-math -funroll-loops
CFLAGS = $(INCFLAGS) $(OPTFLAGS) -g3 -ggdb3
LDFLAGS = -ldl -lm
target = run

srcfiles_all = \
	$(wildcard *.c) \
	$(wildcard modules/*.c)

headerfiles_all = $(wildcard *.h)


objfiles_all = $(srcfiles_all:.c=.o)
depfiles_all = $(objfiles_all:.o=.d)
protofile = prot.inc

# janky mess
havecproto = 1
ifeq (, $(shell which cproto))
havecproto = 0
endif

all: $(protofile) $(target)

## dear god what a kludge
ifeq (1, $(havecproto))
$(protofile): $(srcfiles_all) $(headerfiles_all)
	echo > $(protofile)
	cproto -si $(srcfiles_all) $(headerfiles_all) 2>/dev/null | perl -pe 's/\b_Bool\b/bool/g' | grep -vP __inline > $(protofile)_tmp
	mv $(protofile)_tmp $(protofile)
else
$(protofile): $(srcfiles_all)
endif

$(target): $(objfiles_all)
	$(CC) -o $@ $^ $(LDFLAGS)

-include $(depfiles_all)

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.c
	$(CC) $(CFLAGS) $< -MM -MT $(@:.d=.o) -MF $@

%.o: %.c
	$(CC) $(CFLAGS) -c $(DBGFLAGS) -o $@ $<

.PHONY: clean
clean:
	rm -f $(objfiles_all) $(target)

.PHONY: cleandep
cleandep:
	rm -f $(depfiles_all)

.PHONY: rebuild
rebuild: clean cleandep $(target)

.PHONY: sanity
sanity:
	./run sanity.msl
