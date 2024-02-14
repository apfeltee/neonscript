
INCFLAGS = -I. -Ithirdparty

CC ?= gcc
#WARNFLAGS_OPTIONAL = -Wno-unused-parameter
WARNFLAGS_NEEDED = -Wall -Wextra -Wshadow -Wunused-macros -Wunused-local-typedefs $(WARNFLAGS_OPTIONAL)
CCCMD = $(CC) $(WARNFLAGS_NEEDED)
CFLAGS =

# gottagofast.mp4
#CFLAGS += $(INCFLAGS) -Ofast -march=native -flto -ffast-math -funroll-loops
CFLAGS += $(INCFLAGS) -O0 -g3 -ggdb3

# debug flags
CFLAGS += -ftrapv -fsanitize=undefined

## VERY SLOW!!!!
#CFLAGS += -fanalyzer

LDFLAGS = -flto -ldl -lm  -lreadline -lpthread -lubsan
target = run

srcfiles_all = \
	$(wildcard *.c) \
	linenoise/*.c

srcfiles_prot = $(wildcard *.c)

headerfiles_all =

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
	cproto $(INCFLAGS) $(srcfiles_prot) $(headerfiles_all) 2>/dev/null | perl -pe 's/\b_Bool\b/bool/g' | grep -vP __inline > $(protofile)_tmp
	mv $(protofile)_tmp $(protofile)
else
$(protofile): $(srcfiles_all)
endif

$(target): $(objfiles_all)
	$(CCCMD) -o $@ $^ $(LDFLAGS)

-include $(depfiles_all)

# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.c
	$(CCCMD) $(CFLAGS) $< -MM -MT $(@:.d=.o) -MF $@

%.o: %.c
	$(CCCMD) $(CFLAGS) -c $(DBGFLAGS) -o $@ $<

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
