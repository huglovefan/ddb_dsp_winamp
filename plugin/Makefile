CFLAGS	?= -Og -g
PREFIX	?= $(HOME)/.local

override CFLAGS	:= -Wall -Wextra -Wpedantic \
	-DDDB_API_LEVEL=10 -DDDB_WARN_DEPRECATED=1 -fPIC -shared $(CFLAGS)

.PHONY: install uninstall clean

dsp_winamp.so: ../common.h plugin.c
	$(CC) $(CFLAGS) plugin.c -o $@

install:
	install -Dv dsp_winamp.so $(DESTDIR)$(PREFIX)/lib/deadbeef/dsp_winamp.so

uninstall:
	rm $(DESTDIR)$(PREFIX)/lib/deadbeef/dsp_winamp.so

clean:
	rm dsp_winamp.so
