DEPOPTS = -MMD -MF .$@.dep -MP
CCOPTS = $(DEPOPTS) -ggdb -Wall -pthread -fPIC

IPMILIBS = -I ../../../freeipmi-1.2.2_built/include \
	   -L ../../../freeipmi-1.2.2_built/lib \
	   -lfreeipmi 

all: libsysmgr.so testapp tags

libsysmgr.so: sysmgr.cpp sysmgr.h ../scope_lock.cpp ../scope_lock.h
	g++ $(CCOPTS) $(IPMILIBS) -shared -o $@ sysmgr.cpp ../scope_lock.cpp

testapp: testapp.cpp libsysmgr.so
	g++ $(CCOPTS) -L. -lsysmgr -o $@ testapp.cpp

tags: *.cpp *.h
	ctags -R . 2>/dev/null || true

distclean: clean
	rm -f .*.dep tags
clean:
	rm -f libsysmgr.so testapp

.PHONY: distclean clean all

-include $(wildcard .*.dep)
