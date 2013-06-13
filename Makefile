DEPOPTS = -MMD -MF .$@.dep -MP
CCOPTS = $(DEPOPTS) -ggdb -Wall -pthread

IPMILIBS = -I ../freeipmi-1.2.2_built/include \
	   -L ../freeipmi-1.2.2_built/lib \
	   -lfreeipmi \
	   -lconfuse

all: sysmgr tags

sysmgr: sysmgr.cpp sysmgr.h scope_lock.cpp scope_lock.h TaskQueue.cpp TaskQueue.h Callback.h Crate.cpp Crate.h mgmt_protocol.cpp mgmt_protocol.h WakeSock.h cardindex.h cardindex.inc commandindex.h commandindex.inc $(wildcard cards/*.h) $(wildcard cards/*.cpp) $(wildcard commands/*.h)
	g++ $(CCOPTS) $(IPMILIBS) -o $@ sysmgr.cpp scope_lock.cpp TaskQueue.cpp Crate.cpp mgmt_protocol.cpp $(wildcard cards/*.cpp)

cardindex.h cardindex.inc: configure $(wildcard cards/*.h)
	./configure

commandindex.h commandindex.inc: configure $(wildcard commands/*.h)
	./configure

tags: *.cpp *.h
	ctags -R . 2>/dev/null || true

distclean: clean
	rm -f .*.dep tags cardindex.h cardindex.inc commandindex.h commandindex.inc
clean:
	rm -f sysmgr

.PHONY: distclean clean all

-include $(wildcard .*.dep)
