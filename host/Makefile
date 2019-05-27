CFLAGS	?= -Og -g
PREFIX	?= $(HOME)/.local

WINECC	?= winegcc -m32 -mno-cygwin

WINEPREFIX	?= $(HOME)/.wine
WINAMP_INCDIR	?= $(WINEPREFIX)/drive_c/Program\ Files\ \(x86\)/Winamp\ SDK

override CFLAGS	:= -I $(WINAMP_INCDIR) -Wall -Wextra -Wpedantic $(CFLAGS)

.PHONY: install uninstall clean

ddw_host.exe: ../common.h main.c
	$(WINECC) $(CFLAGS) main.c -o $@

install:
	install -Dv ddw_host.exe $(DESTDIR)$(PREFIX)/bin/ddw_host.exe
	install -Dv ddw_host.exe.so $(DESTDIR)$(PREFIX)/bin/ddw_host.exe.so

uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/ddw_host.exe $(DESTDIR)$(PREFIX)/bin/ddw_host.exe.so

clean:
	rm ddw_host.exe ddw_host.exe.so
