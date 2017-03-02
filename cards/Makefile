DEPOPTS = -MMD -MF .$@.dep -MP
CCOPTS = $(DEPOPTS) -ggdb -Wall -pthread -fPIC

SRC = $(wildcard *.cpp)
OBJ = $(patsubst %.cpp, %.so, $(SRC))

all: cards tags

cards: $(OBJ)

GenericUW.so: GenericUW.cpp
	g++ $(CCOPTS) $(IPMILIB_PATHS) $$(pkg-config --cflags --libs libxml++-2.6) -shared -o $@ $< $(IPMILIB_LINKS)

%.so: %.cpp
	g++ $(CCOPTS) $(IPMILIB_PATHS) -shared -o $@ $< $(IPMILIB_LINKS)

tags: *.cpp *.h
	ctags -R . 2>/dev/null || true

distclean: clean
	rm -f .*.dep tags
clean:
	rm -f *.so

.PHONY: distclean clean all cards

-include $(wildcard .*.dep)
