CFLAGS ?= -O2 -fstack-clash-protection -g
CPPFLAGS ?= -D_FORTIFY_SOURCE=2

DESTDIR     ?=
PREFIX      ?= $(HOME)/.local
EXEC_PREFIX ?= $(PREFIX)
BINDIR      ?= $(EXEC_PREFIX)/bin
LIBDIR      ?= $(EXEC_PREFIX)/lib

override CFLAGS := -Wall -Wextra -Wpedantic -Wdeclaration-after-statement -std=c99 \
                   -fPIC -shared \
                   $(CFLAGS)

override CPPFLAGS := -DDDB_API_LEVEL=10 -DDDB_WARN_DEPRECATED=1 \
                     -D_DEFAULT_SOURCE \
                     $(CPPFLAGS)

.PHONY: install uninstall clean

dsp_winamp.so: ../common.h plugin.c
	$(CC) $(LDFLAGS) $(CFLAGS) $(CPPFLAGS) plugin.c -o $@

install:
	@install -Dv dsp_winamp.so $(DESTDIR)$(LIBDIR)/deadbeef/dsp_winamp.so

uninstall:
	@rm -v $(DESTDIR)$(LIBDIR)/deadbeef/dsp_winamp.so

clean:
	-@rm -v dsp_winamp.so
