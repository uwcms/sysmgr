DEPOPTS = -MMD -MF .$@.dep -MP
CCOPTS = $(DEPOPTS) -ggdb -Wall -pthread -fPIC

IPMILIBS := -lfreeipmi -lconfuse $(IPMILIBS)

SRC = $(wildcard *.cpp)
OBJ = $(patsubst %.cpp, %.so, $(SRC))

all: cards tags

cards: $(OBJ)

GenericUW.so: GenericUW.cpp
	g++ $(CCOPTS) $(IPMILIBS) $$(pkg-config --cflags --libs libxml++-2.6) -shared -o $@ $<

%.so: %.cpp
	g++ $(CCOPTS) $(IPMILIBS) -shared -o $@ $<

tags: *.cpp *.h
	ctags -R . 2>/dev/null || true

distclean: clean
	rm -f .*.dep tags
clean:
	rm -f *.so

.PHONY: distclean clean all cards

-include $(wildcard .*.dep)
