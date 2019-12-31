CFLAGS ?= -O2 -fstack-clash-protection -fstack-protector-strong -ggdb3
PREFIX ?= $(HOME)/.local

ifeq (, $(WINCC))
 ifneq (, $(shell sh -c 'command -v i686-w64-mingw32-gcc'))
  WINCC := i686-w64-mingw32-gcc
 else
  ifneq (, $(shell sh -c 'command -v winegcc'))
   WINCC := winegcc -m32 -mno-cygwin
   override CFLAGS := $(CFLAGS) -fno-lto
   override LDFLAGS := $(LDFLAGS) -fno-lto
  else
   $(error No usable compiler found)
  endif
 endif
endif

WINAMP_INCDIR ?= ../Winamp SDK

override CFLAGS := -I "$(WINAMP_INCDIR)" \
                   -Wall -Wextra -Wpedantic -Wdeclaration-after-statement -std=gnu99 \
                   -fno-strict-aliasing \
                   $(CFLAGS)

.PHONY: install uninstall clean

ddw_host.exe: ../common.h main.c
	$(WINCC) $(LDFLAGS) $(CFLAGS) $(CPPFLAGS) main.c -o $@

install:
	@install -Dv ddw_host.exe $(DESTDIR)$(PREFIX)/bin/ddw_host.exe; \
	if [ -e ddw_host.exe.so ]; then install -Dv ddw_host.exe.so $(DESTDIR)$(PREFIX)/bin/ddw_host.exe.so; fi

uninstall:
	@rm -v $(DESTDIR)$(PREFIX)/bin/ddw_host.exe; \
	if [ -e $(DESTDIR)$(PREFIX)/bin/ddw_host.exe.so ]; then rm -v $(DESTDIR)$(PREFIX)/bin/ddw_host.exe.so; fi

clean:
	-@rm -v ddw_host.exe; \
	if [ -e ddw_host.exe.so ]; then rm -v ddw_host.exe.so; fi
