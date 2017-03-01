DEPOPTS = -MMD -MF .$@.dep -MP
CCOPTS = $(DEPOPTS) -ggdb -Wall -pthread -fPIC


all: libsysmgr.so testapp tags

libsysmgr.so: sysmgr.cpp sysmgr.h ../scope_lock.cpp ../scope_lock.h
	g++ $(CCOPTS) $(IPMILIB_PATHS) -shared -o $@ sysmgr.cpp ../scope_lock.cpp

testapp: testapp.cpp libsysmgr.so
	g++ $(CCOPTS) -L. -o $@ testapp.cpp -lsysmgr

tags: *.cpp *.h
	ctags -R . 2>/dev/null || true

distclean: clean
	rm -f .*.dep tags
clean:
	rm -f libsysmgr.so testapp
	rm -rf __pycache__/ *.py[cod]

.PHONY: distclean clean all

-include $(wildcard .*.dep)
