DEPOPTS = -MMD -MF .dep/$(subst /,^,$(subst .obj/,,$@)).d -MP
CCOPTS = $(DEPOPTS) -ggdb -Wall -pthread

IPMILIB_PATHS := $(IPMILIB_PATHS)
IPMILIB_LINKS := $(IPMILIB_LINKS) -lfreeipmi -lconfuse
LIBS = $(IPMILIB_LINKS) -ldl

all: sysmgr clientapi cards sysmgr.example.conf tags

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

.configured: configure.py $(wildcard commands/*.h) sysmgr.example.tmpl.conf $(wildcard cards/*.cpp) sysmgr.tmpl.spec
	./configure.py
	touch .configured

commandindex.h: .configured
commandindex.inc: .configured
sysmgr.example.conf: .configured

clientapi:
	$(MAKE) -C clientapi all

tags: *.cpp *.h
	ctags -R . 2>/dev/null || true

distclean: clean rpmclean
	rm -rf .configured tags commandindex.h commandindex.inc sysmgr.example.conf sysmgr.spec .dep/
	$(MAKE) -C clientapi distclean
	$(MAKE) -C cards distclean
clean:
	rm -f sysmgr
	rm -rf .obj/
	rm -rf rpmbuild/
	$(MAKE) -C clientapi clean
	$(MAKE) -C cards clean
rpmclean:
	rm -rf *.rpm rpms/ rpmbuild/

sysmgr.spec: .configured

rpm: all sysmgr.spec
	SYSMGR_ROOT=$(PWD) rpmbuild -ba --quiet --define "_topdir $(PWD)/rpmbuild" sysmgr.spec
	rm -rf rpms/
	mkdir -p rpms/
	cp -v $(PWD)/rpmbuild/RPMS/*/*.rpm rpms/
	rm -rf rpmbuild/
	@echo
	@echo '*** Don'\''t forget to run `make rpmsign`!'

ifneq ("$(wildcard rpms/*.rpm)","")
rpmsign: $(wildcard rpms/*.rpm)
else
rpmsign: rpms
endif
	rpmsign --macros='/usr/lib/rpm/macros:/usr/lib/rpm/redhat/macros:/etc/rpm/macros:$(HOME)/.rpmmacros' --addsign rpms/*.rpm

.PHONY: distclean clean rpmclean all clientapi rpm rpmsign cards

-include $(wildcard .dep/*)
