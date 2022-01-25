all: both one
both: plugin shm
one: host

plugin: dsp_winamp.so
shm: ddb_shm.so
host: ddw-host-d.exe

dflags += -g -gs

ifeq (1,$(opt))
 dflags += -O -mcpu=avx
 dflags_posix += -L-O2 -L--gc-sections -L--hash-style=gnu
 dflags_windows += -L/opt:icf -L/opt:ref
else
 dflags_posix += -L-O0 -L--no-gc-sections -L--hash-style=gnu
 dflags_windows += -L/opt:noicf -L/opt:noref
endif

ifeq (1,$(debug))
 dflags += -debug
endif

.PHONY: dsp_winamp.so
dsp_winamp.so:
	dmd -i -shared $(dflags) $(dflags_posix) -mv=ddw=src -mv=misclib=src/common/misclib src/plugin/main.d src/c_deadbeef.c -of=$@ && size $@

.PHONY: ddb_shm.so
ddb_shm.so:
	dmd -i -shared $(dflags) $(dflags_posix) -mv=ddw=src -mv=misclib=src/common/misclib src/shm/main.d src/c_deadbeef.c -of=$@ && size $@

.PHONY: ddw-host-d.exe
ddw-host-d.exe:
	wine dmd -i -m32mscoff $(dflags) $(dflags_windows) -mv=ddw=src src/host/main.d -Lntdll.lib -Luser32.lib -of=$@ && size $@

test:
	dmd -i -unittest -main $(dflags) $(dflags_posix) -mv=ddw=src -mv=misclib=src/common/misclib src/plugin/main.d src/c_deadbeef.c -of=unittest && ./unittest
	dmd -i -unittest -main $(dflags) $(dflags_posix) -mv=ddw=src -mv=misclib=src/common/misclib src/shm/main.d src/c_deadbeef.c -of=unittest && ./unittest
	wine dmd -i -m32mscoff -unittest $(dflags) $(dflags_windows) -mv=ddw=src src/host/main.d -Lntdll.lib -Luser32.lib -of=unittest.exe && wine ./unittest.exe

install:
	@cp -v dsp_winamp.so ~/.local/lib/deadbeef/dsp_winamp.so.tmp; \
	mv -v ~/.local/lib/deadbeef/dsp_winamp.so.tmp ~/.local/lib/deadbeef/dsp_winamp.so; \
	cp -v ddb_shm.so ~/.local/lib/deadbeef/ddb_shm.so.tmp; \
	mv -v ~/.local/lib/deadbeef/ddb_shm.so.tmp ~/.local/lib/deadbeef/ddb_shm.so; \
	cp -v ddw-host-d.exe ~/.local/bin/ddw-host-d.exe.tmp; \
	mv -v ~/.local/bin/ddw-host-d.exe.tmp ~/.local/bin/ddw-host-d.exe

# dmd 2.098.1:
# - unwrap this typedef: typedef struct DB_output_s DB_output_t;
#   https://issues.dlang.org/show_bug.cgi?id=22625
# master:
# - remove lines matching /^struct .*;$/
#   https://issues.dlang.org/show_bug.cgi?id=22705
src/c_deadbeef.c:
	cpp -P -std=c11 -DDDB_API_LEVEL=10 -D__asm__\(x\)= -D__restrict= /usr/include/deadbeef/deadbeef.h >$@

clean:
	@rm -fv ./*.exe ./*.o ./*.obj ./*.pdb ./*.so ./unittest
