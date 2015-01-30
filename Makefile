DEPOPTS = -MMD -MF .$@.dep -MP
CCOPTS = $(DEPOPTS) -ggdb -Wall -pthread

IPMILIBS := -lfreeipmi -lconfuse $(IPMILIBS)

all: sysmgr clientapi cards sysmgr.conf.example tags

sysmgr: sysmgr.cpp sysmgr.h mprintf.cpp scope_lock.cpp scope_lock.h TaskQueue.cpp TaskQueue.h Callback.h Crate.cpp Crate.h mgmt_protocol.cpp mgmt_protocol.h WakeSock.h commandindex.h commandindex.inc $(wildcard commands/*.h)
	g++ $(CCOPTS) $(IPMILIBS) -ldl -rdynamic -o $@ sysmgr.cpp mprintf.cpp scope_lock.cpp TaskQueue.cpp Crate.cpp mgmt_protocol.cpp

cards: sysmgr
	make -C cards all

commandindex.h commandindex.inc: configure $(wildcard commands/*.h)
	./configure

sysmgr.conf.example: sysmgr.conf.example.tmpl $(wildcard cards/*.cpp)
	./configure

clientapi:
	make -C clientapi all

tags: *.cpp *.h
	ctags -R . 2>/dev/null || true

distclean: clean
	rm -f .*.dep tags commandindex.h commandindex.inc *.rpm sysmgr.conf.example
	make -C clientapi distclean
	make -C cards distclean
clean:
	rm -f sysmgr
	rm -rf rpm/
	make -C clientapi clean
	make -C cards clean

rpm: all
	SYSMGR_ROOT=$(PWD) rpmbuild --sign -ba --quiet --define "_topdir $(PWD)/rpm" sysmgr.spec
	cp -v $(PWD)/rpm/RPMS/*/*.rpm ./

.PHONY: distclean clean all clientapi rpm cards

-include $(wildcard .*.dep)
