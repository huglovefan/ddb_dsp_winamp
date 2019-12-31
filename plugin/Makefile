CFLAGS ?= -O2 -fstack-clash-protection -fstack-protector-strong -ggdb3
PREFIX ?= $(HOME)/.local

override CFLAGS := -Wall -Wextra -Wpedantic -Wdeclaration-after-statement -std=c99 \
                   -fPIC -shared \
                   $(CFLAGS)

override CPPFLAGS := -DDDB_API_LEVEL=10 -DDDB_WARN_DEPRECATED=1 \
                     -D_DEFAULT_SOURCE \
                     $(CPPFLAGS)

.PHONY: install uninstall clean

dsp_winamp.so: ../common.h plugin.c
	$(CC) $(LDFLAGS) $(CFLAGS) $(CPPFLAGS) plugin.c -o $@

# todo: /lib part should be configurable
install:
	@install -Dv dsp_winamp.so $(DESTDIR)$(PREFIX)/lib/deadbeef/dsp_winamp.so

uninstall:
	@rm -v $(DESTDIR)$(PREFIX)/lib/deadbeef/dsp_winamp.so

clean:
	-@rm -v dsp_winamp.so
