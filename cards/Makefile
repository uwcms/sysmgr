DEPOPTS = -MMD -MF .$@.dep -MP
CCOPTS = $(DEPOPTS) -ggdb -Wall -pthread -fPIC

SRC = $(wildcard *.cpp)
OBJ = $(patsubst %.cpp, %.so, $(SRC))

all: cards

cards: $(OBJ)

UWDirect.so: UWDirect.cpp
	g++ $(CCOPTS) $(IPMILIB_PATHS) $$(pkg-config --cflags --libs libxml++-2.6) -shared -o $@ $< $(IPMILIB_LINKS)

GenericUW.so: GenericUW.cpp
	g++ $(CCOPTS) $(IPMILIB_PATHS) $$(pkg-config --cflags --libs libxml++-2.6) -shared -o $@ $< $(IPMILIB_LINKS)

%.so: %.cpp
	g++ $(CCOPTS) $(IPMILIB_PATHS) -shared -o $@ $< $(IPMILIB_LINKS)

distclean: clean
	rm -f .*.dep
clean:
	rm -f *.so

.PHONY: distclean clean all cards

-include $(wildcard .*.dep)
