DEPOPTS = -MMD -MF .$@.dep -MP
CCOPTS = $(DEPOPTS) -ggdb -Wall -pthread

IPMILIBS = -lfreeipmi -lconfuse

all: sysmgr clientapi tags

sysmgr: sysmgr.cpp sysmgr.h mprintf.cpp scope_lock.cpp scope_lock.h TaskQueue.cpp TaskQueue.h Callback.h Crate.cpp Crate.h mgmt_protocol.cpp mgmt_protocol.h WakeSock.h cardindex.h cardindex.inc commandindex.h commandindex.inc $(wildcard cards/*.h) $(wildcard cards/*.cpp) $(wildcard commands/*.h)
	g++ $(CCOPTS) $(IPMILIBS) -o $@ sysmgr.cpp mprintf.cpp scope_lock.cpp TaskQueue.cpp Crate.cpp mgmt_protocol.cpp $(wildcard cards/*.cpp)

cardindex.h cardindex.inc: configure $(wildcard cards/*.h)
	./configure

commandindex.h commandindex.inc: configure $(wildcard commands/*.h)
	./configure

clientapi:
	make -C clientapi all

tags: *.cpp *.h
	ctags -R . 2>/dev/null || true

distclean: clean
	rm -f .*.dep tags cardindex.h cardindex.inc commandindex.h commandindex.inc *.rpm
	make -C clientapi distclean
clean:
	rm -f sysmgr
	rm -rf rpm/
	make -C clientapi clean

rpm: all
	SYSMGR_ROOT=$(PWD) rpmbuild --sign -ba --quiet --define "_topdir $(PWD)/rpm" sysmgr.spec
	cp -v $(PWD)/rpm/RPMS/*/*.rpm ./

.PHONY: distclean clean all clientapi rpm

-include $(wildcard .*.dep)
