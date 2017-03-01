DEPOPTS = -MMD -MF .dep/$(subst /,^,$(subst .obj/,,$@)).d -MP
CCOPTS = $(DEPOPTS) -ggdb -Wall -pthread

IPMILIB_PATHS := $(IPMILIB_PATHS)
IPMILIB_LINKS := $(IPMILIB_LINKS) -lfreeipmi -lconfuse
LIBS = $(IPMILIB_LINKS) -ldl

all: sysmgr clientapi cards sysmgr.conf.example tags

sysmgr: .obj/sysmgr.o .obj/mprintf.o .obj/scope_lock.o .obj/TaskQueue.o .obj/Crate.o .obj/mgmt_protocol.o .obj/versioninfo.o
	g++ $(CCOPTS) $(IPMILIB_PATHS) -rdynamic -o $@ $^ $(LIBS)

#.PHONY: .obj/versioninfo.o
.obj/versioninfo.o: $(shell git ls-files)
	@mkdir -p .dep/ "$(dir $@)"
	echo "const char *GIT_BRANCH = \"$$(git rev-parse --abbrev-ref HEAD)\"; const char *GIT_COMMIT = \"$$(git describe)\"; const char *GIT_DIRTY = \"$$(git status --porcelain -z | sed -re 's/\x0/\\\\n/g')\";" | tee -a /dev/stderr | g++ $(CCOPTS) $(DEPOPTS) -c -o $@ -xc++ -

.obj/mgmt_protocol.o: mgmt_protocol.cpp commandindex.h commandindex.inc

.obj/%.o: %.cpp
	@mkdir -p .dep/ "$(dir $@)"
	g++ $(CCOPTS) $(DEPOPTS) $(IPMILIB_PATHS) -c -o $@ $<

cards: sysmgr
	$(MAKE) -C cards all

.configured: configure $(wildcard commands/*.h) sysmgr.conf.example.tmpl $(wildcard cards/*.cpp)
	./configure
	-touch .configured

commandindex.h commandindex.inc sysmgr.conf.example: .configured

clientapi:
	$(MAKE) -C clientapi all

tags: *.cpp *.h
	ctags -R . 2>/dev/null || true

distclean: clean
	rm -rf tags commandindex.h commandindex.inc *.rpm sysmgr.conf.example .dep/
	$(MAKE) -C clientapi distclean
	$(MAKE) -C cards distclean
clean:
	rm -f sysmgr
	rm -f .configured
	rm -rf .obj/
	rm -rf rpm/
	$(MAKE) -C clientapi clean
	$(MAKE) -C cards clean

rpm: all
	SYSMGR_ROOT=$(PWD) rpmbuild -ba --quiet --define "_topdir $(PWD)/rpm" sysmgr.spec
	cp -v $(PWD)/rpm/RPMS/*/*.rpm ./
	rm -rf rpm/
	@echo
	@echo '*** Don'\''t forget to run `make rpmsign`!'

rpmsign: rpm
	rpmsign --macros='/usr/lib/rpm/macros:/usr/lib/rpm/redhat/macros:/etc/rpm/macros:$(HOME)/.rpmmacros' --addsign *.rpm

.PHONY: distclean clean all clientapi rpm rpmsign cards

-include $(wildcard .dep/*)
