all: plugin host

ez:
	make testpl
	make testhst
	make plugin opt=1 debug=1 dmd=ldc2
	make host opt=1 debug=1
	make install

plugin: dsp_winamp.so
host: ddw-host-d.exe

dmd ?= dmd
size ?= size

#dflags += -checkaction=halt

ifeq (dmd,$(notdir $(dmd)))
 dflags_posix += -L-lphobos2
 dflags_windows += -m32mscoff
 ifeq (1,$(opt))
  dflags += -O -g -gs -mcpu=avx
 else
  dflags += -g -gs
 endif
 ifeq (1,$(debug))
  dflags += -debug
  dflags += -checkaction=D
 endif
endif

ifeq (ldc2,$(notdir $(dmd)))
 dflags += --link-defaultlib-shared
 dflags_windows += -m32
 ifeq (1,$(opt))
  dflags += --O2 -g --enable-inlining=false --frame-pointer=all --mcpu=bdver3 --mattr=-bmi,-xop
  dflags_posix += --linker=lld
 else
  dflags += --O0 -g --disable-linker-strip-dead --frame-pointer=all --mcpu=bdver3 --mattr=-bmi,-xop
  dflags_posix += --linker=lld
 endif
 ifeq (1,$(debug))
  dflags += --d-debug
  dflags += -checkaction=D
 endif
endif

ifeq (1,$(opt))
 dflags_posix += -L-O2 -L--gc-sections -L--hash-style=gnu
 dflags_windows += -L/opt:icf -L/opt:ref
else
 dflags_posix += -L-O0 -L--no-gc-sections -L--hash-style=gnu
 dflags_windows += -L/opt:noicf -L/opt:noref
endif

.PHONY: dsp_winamp.so
dsp_winamp.so:
	$(dmd) -i -shared $(dflags) $(dflags_posix) $(mydflags) -mv=ddw=src -mv=misclib=src/common/misclib src/plugin/main.d -of=$@ && $(size) $@

.PHONY: ddw-host-d.exe
ddw-host-d.exe:
	wine $(dmd) -i $(dflags) $(dflags_windows) $(mydflags) -mv=ddw=src -mv=misclib=src/common/misclib src/host/entry.d -Lntdll.lib -Luser32.lib -of=$@ && $(size) $@

watchpl:
	find src/ -name '*.[cd]' | entr -cr ttl make -s plugin
watchhst:
	find src/ -name '*.[cd]' | entr -cr ttl make -s host

test: testpl testhost

testpl:
	$(dmd) -i -unittest -main $(dflags) $(dflags_posix) -checkaction=D $(mydflags) -mv=ddw=src -mv=misclib=src/common/misclib src/plugin/main.d -of=unittest && ./unittest

testhst:
	wine $(dmd) -i -unittest $(dflags) $(dflags_windows) -checkaction=D $(mydflags) -mv=ddw=src -mv=misclib=src/common/misclib src/host/entry.d -Lntdll.lib -Luser32.lib -of=unittest.exe && wine ./unittest.exe

install:
	@cp -v dsp_winamp.so ~/.local/lib/deadbeef/dsp_winamp.so.tmp; \
	mv -v ~/.local/lib/deadbeef/dsp_winamp.so.tmp ~/.local/lib/deadbeef/dsp_winamp.so; \
	cp -v ddw-host-d.exe ~/.local/bin/ddw-host-d.exe.tmp; \
	mv -v ~/.local/bin/ddw-host-d.exe.tmp ~/.local/bin/ddw-host-d.exe

# dmd 2.098.1, ldc 1.28.1:
# - unwrap this typedef: typedef struct DB_output_s DB_output_t;
#   https://issues.dlang.org/show_bug.cgi?id=22625
c_deadbeef.c:
	cpp -P -std=c11 -DDDB_API_LEVEL=10 -D__asm__\(x\)= -D__restrict= /usr/include/deadbeef/deadbeef.h >$@

clean:
	@rm -fv ./*.exe ./*.o ./*.obj ./*.pdb ./*.so ./unittest
