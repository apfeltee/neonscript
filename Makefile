
INCFLAGS = -I. -Ithirdparty

CXX = g++
#WARNFLAGS_OPTIONAL = -Wno-unused-parameter
WARNFLAGS_NEEDED = -Wall -Wextra -Wshadow -Wunused-macros -Wunused-local-typedefs $(WARNFLAGS_OPTIONAL)
CCCMD = $(CXX) $(WARNFLAGS_NEEDED)
CFLAGS =

# gottagofast.mp4
#CFLAGS += $(INCFLAGS) -Ofast -march=native -flto -ffast-math -funroll-loops
#CFLAGS += -ftrapv -fsanitize=undefined

CFLAGS += $(INCFLAGS) -O0 -g3 -ggdb3

# debug flags
CFLAGS +=

LDFLAGS =
LDFLAGS += -flto -ldl -lm  -lreadline -lpthread
#LDFLAGS += -lubsan

target = run

srcfiles_all = $(wildcard *.cpp)

srcfiles_prot = $(wildcard *.cpp)

headerfiles_all =

objfiles_all = $(srcfiles_all:.cpp=.o)
depfiles_all = $(objfiles_all:.o=.d)
protofile = prot.inc

# janky mess
havecproto = 1
ifeq (, $(shell which cproto))
havecproto = 0
endif

all: $(protofile) $(target)


$(target): $(objfiles_all)
	$(CCCMD) -o $@ $^ $(LDFLAGS)


-include $(depfiles_all)


# rule to generate a dep file by using the C preprocessor
# (see man cpp for details on the -MM and -MT options)
%.d: %.cpp
	$(CCCMD) $(CFLAGS) $< -MM -MT $(@:.d=.o) -MF $@

%.o: %.cpp
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
