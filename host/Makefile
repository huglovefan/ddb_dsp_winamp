CPPFLAGS ?= -D_GLIBCXX_ASSERTIONS
CXXFLAGS ?= -O2 -fstack-clash-protection -fstack-protector-strong -g -pipe

WINCXX ?= i686-w64-mingw32-g++

DESTDIR     ?=
PREFIX      ?= $(HOME)/.local
EXEC_PREFIX ?= $(PREFIX)
BINDIR      ?= $(EXEC_PREFIX)/bin
LIBDIR      ?= $(EXEC_PREFIX)/lib

WINAMP_INCDIR ?= ../Winamp SDK

override CPPFLAGS := -I "$(WINAMP_INCDIR)" \
                     -D WIN32_LEAN_AND_MEAN \
                     $(CPPFLAGS)

override CXXFLAGS := -Wall -Wextra -Wpedantic \
                     -std=gnu++20 \
                     $(CXXFLAGS)

override LDFLAGS := -static \
                    $(LDFLAGS)

override CXXFLAGS := -fstack-protector-strong  $(CXXFLAGS)

ifeq ($(ANAL), 1)
  override CXXFLAGS := -fanalyzer  $(CXXFLAGS)
endif

override CXXFLAGS := -flto=auto  $(CXXFLAGS)
override LDFLAGS  := $(CXXFLAGS) $(LDFLAGS)

.PHONY: install uninstall clean test

OBJS = main.o plugin.o thread.o dll.o

ddw_host.exe: $(OBJS)
	$(WINCXX) $(LDFLAGS) $^ -o $@

.cpp.o:
	$(WINCXX) -c $(CXXFLAGS) $(CPPFLAGS) $< -o $@

install:
	@install -Dv ddw_host.exe $(DESTDIR)$(BINDIR)/ddw_host.exe

uninstall:
	@rm -v $(DESTDIR)$(BINDIR)/ddw_host.exe

clean:
	-@rm -v ddw_host.exe

test:
	cd .. && sh test.sh
