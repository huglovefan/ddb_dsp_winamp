
all: plugin host
test: testplugin testhost

install:
	@mkdir -pv ~/.local/lib/deadbeef; \
	cp -v dsp_winamp.so ~/.local/lib/deadbeef/dsp_winamp.so.tmp; \
	mv -v ~/.local/lib/deadbeef/dsp_winamp.so.tmp ~/.local/lib/deadbeef/dsp_winamp.so; \
	mkdir -pv ~/.local/bin; \
	cp -v ddw-host-d.exe ~/.local/bin/ddw-host-d.exe.tmp; \
	mv -v ~/.local/bin/ddw-host-d.exe.tmp ~/.local/bin/ddw-host-d.exe

clean:
	@rm -fv ./*.exe ./*.i ./*.o ./*.obj ./*.so ./*.pdb

## -----------------------------------------------------------------------------

plugin: dsp_winamp.so

PLUGIN_SRCS = \
	c_deadbeef.i \
	src/common/gc.d \
	src/common/misclib/druntime/threadinit.d \
	src/common/pipedata.d \
	src/common/rtopts.d \
	src/common/shmdata.d \
	src/plugin/child.d \
	src/plugin/chldinit.d \
	src/plugin/chldproc.d \
	src/plugin/deadbeef.d \
	src/plugin/fmt.d \
	src/plugin/main.d \
	src/plugin/misc.d \
	src/plugin/shm.d \
	src/plugin/tickmain.d \

dsp_winamp.so: $(PLUGIN_SRCS)
	dmd -shared -O -g -inline -defaultlib=libphobos2.so $^ -of=$@ && size $@

.PHONY: testplugin
testplugin: $(PLUGIN_SRCS)
	dmd -unittest -main -O -g -inline -defaultlib=libphobos2.so $^ -of=$@ && ./$@ && rm -f $@
watchplugin:
	ls $(PLUGIN_SRCS) | entr -cs 'make plugin'

c_deadbeef.i:
	cpp -DDDB_API_LEVEL=15 -D__asm__=asm -D__restrict= -I$$HOME/git -include deadbeef/deadbeef.h /dev/null >$@

## -----------------------------------------------------------------------------

host: ddw-host-d.exe

HOST_SRCS = \
	src/common/gc.d \
	src/common/pipedata.d \
	src/common/rtopts.d \
	src/common/shmdata.d \
	src/host/buf.d \
	src/host/entry.d \
	src/host/fmt.d \
	src/host/main.d \
	src/host/misc.d \
	src/host/plugin.d \
	src/host/plugproc.d \
	src/host/plugload.d \
	src/host/procmain.d \
	src/host/shm.d \
	src/host/winamp.d \
	src/host/wndproc.d \

ddw-host-d.exe: $(HOST_SRCS)
	wine dmd -m32 -O -g -inline -L=user32.lib -L=ntdll.lib $^ -of=$@ && size $@

.PHONY: testhost
testhost: $(HOST_SRCS)
	wine dmd -unittest -m32 -O -g -inline -L=user32.lib -L=ntdll.lib $^ -of=$@.exe && ./$@.exe && rm -f $@.exe
watchhost:
	ls $(HOST_SRCS) | entr -cs 'make host'
