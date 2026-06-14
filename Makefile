
all: plugin host

install:
	@mkdir -pv ~/.local/lib/deadbeef; \
	cp -v dsp_winamp.so ~/.local/lib/deadbeef/dsp_winamp.so.tmp; \
	mv -v ~/.local/lib/deadbeef/dsp_winamp.so.tmp ~/.local/lib/deadbeef/dsp_winamp.so; \
	mkdir -pv ~/.local/bin; \
	cp -v ddw-host-d.exe ~/.local/bin/ddw-host-d.exe.tmp; \
	mv -v ~/.local/bin/ddw-host-d.exe.tmp ~/.local/bin/ddw-host-d.exe

uninstall:
	@rm -fv ~/.local/lib/deadbeef/dsp_winamp.so; \
	rm -fv ~/.local/bin/ddw-host-d.exe

clean:
	@rm -frv ./*.exe ./*.so ./.lib

## ---------------------------------------------------------------------

plugin: dsp_winamp.so

plugin_OBJS = \
	.lib/plugin/src/plugin/chldinit.o \
	.lib/plugin/src/plugin/chldproc.o \
	.lib/plugin/src/plugin/main.o \
	.lib/plugin/src/plugin/misc.o \
	.lib/plugin/src/plugin/shm.o \
	.lib/plugin/src/plugin/fmt.o \
	.lib/plugin/src/afmt.o \
	.lib/plugin/src/sfmt.o \
	.lib/plugin/src/monotime.o \
	.lib/plugin/src/crc32.o \

$(plugin_OBJS): src/*.h* src/plugin/*.h*

plugin_CC = cc -pthread -fPIC -shared

plugin_CFLAGS += -O2 -g -gno-record-gcc-switches -march=nocona
plugin_CFLAGS += -finput-charset=ascii
plugin_CFLAGS += -std=c99
plugin_CFLAGS += -fno-asm
plugin_CFLAGS += -fvisibility=hidden
plugin_CFLAGS += -pedantic
plugin_CFLAGS += -Isrc/common
plugin_CFLAGS += -DDDB_API_LEVEL=19
plugin_CFLAGS += -DDDB_WARN_DEPRECATED=1

plugin_CFLAGS += \
	-fno-omit-frame-pointer \
	-fsanitize=undefined \
	-fno-sanitize-recover=all \
	-fsanitize-trap=all \
	-D_FORTIFY_SOURCE=3 \
	-D_GLIBCXX_ASSERTIONS \
	-fstack-protector-strong \
	-fstack-clash-protection \

ifneq ($(DEADBEEF_INCLUDE),)
 plugin_CFLAGS += -isystem$(DEADBEEF_INCLUDE)
endif

plugin_CFLAGS += $(warns) $(cwarns)

plugin_LDFLAGS += -Wl,-z,defs

.lib/plugin/src/%.o: src/%.c
	mkdir -p $(dir $@)
	$(plugin_CC) -c $< $(plugin_CFLAGS) -o $@

dsp_winamp.so: $(plugin_OBJS)
	$(plugin_CC) $^ $(plugin_LDFLAGS) -o $@ $(plugin_LIBS)

$(plugin_OBJS): src/*/*.hpp

watchplugin:
	find src/ -name '*.[ch]*' | entr -cs 'make plugin'

## ---------------------------------------------------------------------

host: ddw-host-d.exe

host_OBJS = \
	.lib/host/src/io.o \
	.lib/host/src/host/entry.o \
	.lib/host/src/host/main.o \
	.lib/host/src/host/plugload.o \
	.lib/host/src/host/wndproc.o \
	.lib/host/src/host/procmain.o \
	.lib/host/src/host/plugproc.o \
	.lib/host/src/host/plugconv.o \
	.lib/host/src/host/plugrest.o \
	.lib/host/src/host/buf.o \
	.lib/host/src/conv.o \
	.lib/host/src/afmt.o \
	.lib/host/src/sfmt.o \
	.lib/host/src/host/shm.o \
	.lib/host/src/host/misc.o \
	.lib/host/src/monotime.o \
	.lib/host/src/host/fp_control.o \
	.lib/host/src/host/fastprintf.o \
	.lib/host/src/host/ipc_hooks.o \
	.lib/host/src/crc32.o \

imgui_OBJS = \
	.lib/host/3p/imgui/imgui.o \
	.lib/host/3p/imgui/imgui_draw.o \
	.lib/host/3p/imgui/imgui_tables.o \
	.lib/host/3p/imgui/imgui_widgets.o \
	.lib/host/3p/imgui/backends/imgui_impl_win32.o \
	.lib/host/3p/imgui/backends/imgui_impl_opengl3.o \
	.lib/host/3p/imgui/backends/imgui_impl_dx9.o \

$(host_OBJS): src/*.h* src/*/*.h*

host_unittest_OBJS = $(host_OBJS:%.o=%_unittest.o)
$(host_unittest_OBJS): host_CFLAGS += -include src/unittest.h
$(host_unittest_OBJS): host_CXXFLAGS += -include src/unittest.h

host_CXXFLAGS += -isystem 3p/imgui

host_CXXFLAGS += -DIMGUI_DISABLE_DEMO_WINDOWS
host_CXXFLAGS += -DIMGUI_DISABLE_DEBUG_TOOLS
# imgui_impl_win32.cpp
host_CXXFLAGS += -DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD
# imgui_impl_dx9.cpp
host_CXXFLAGS += -DIMGUI_USE_BGRA_PACKED_COLOR
host_CXXFLAGS += -DIMGUI_OVERRIDE_DRAWVERT_STRUCT_LAYOUT='struct ImDrawVert{ImVec2 pos;float z;ImU32 col;ImVec2 uv;}'

host_CC ?= i686-w64-mingw32-gcc-win32 -municode -mwindows
host_CXX ?= i686-w64-mingw32-g++-win32 -municode -mwindows

host_flags += -O2 -g -march=nocona
host_flags += -finput-charset=ascii
host_flags += -fno-asm
host_flags += -mfpmath=sse
host_flags += -DWIN32_LEAN_AND_MEAN

host_flags += \
	-fno-omit-frame-pointer \
	-fsanitize=undefined \
	-fno-sanitize-recover=all \
	-fsanitize-trap=all \
	-D_FORTIFY_SOURCE=3 \
	-D_GLIBCXX_ASSERTIONS \
	-fstack-protector-strong \
	-fstack-clash-protection \

host_CFLAGS += -std=c99
host_CFLAGS += -pedantic
host_CFLAGS += $(host_flags)
host_CFLAGS += $(warns) $(cwarns)

host_CXXFLAGS += -std=c++20
host_CXXFLAGS += $(host_flags)
host_CXXFLAGS += $(warns) $(cxxwarns)

host_LDFLAGS += -Wl,--no-insert-timestamp
host_LIBS += -lntdll -ldwmapi -ld3d9 -static-libstdc++ -static-libgcc

ddw-host-d-unittest.exe: host_LDFLAGS += -mconsole

.lib/host/%.o .lib/host/%_unittest.o: %.c
	mkdir -p $(dir $@)
	$(host_CC) -c $< $(host_CFLAGS) -o $@

.lib/host/%.o .lib/host/%_unittest.o: %.cpp
	mkdir -p $(dir $@)
	$(host_CXX) -c $< $(host_CXXFLAGS) -o $@

ddw-host-d.exe: $(host_OBJS) $(imgui_OBJS)
	$(host_CXX) $^ $(host_LDFLAGS) -o $@.tmp $(host_LIBS)
	mv -f $@.tmp $@

ddw-host-d-unittest.exe: $(host_unittest_OBJS) $(imgui_OBJS)
	$(host_CXX) $^ $(host_LDFLAGS) -o $@.tmp $(host_LIBS)
	mv -f $@.tmp $@

testhost: ddw-host-d-unittest.exe
	wine ddw-host-d-unittest.exe

watchhost:
	find src/ -name '*.[ch]*' | entr -cs 'sleep 0.001; make ddw-host-d.exe'

## ---------------------------------------------------------------------

testconv_OBJS = \
	.lib/testconv/src/bin/testconv.o \
	.lib/testconv/src/conv.o \
	.lib/testconv/src/sfmt.o \

testconv_CC += gcc -m32
#~ testconv_CC += clang
#~ testconv_CC += tcc

testconv_CFLAGS += -std=c99
testconv_CFLAGS += -O2 -g -march=nocona -mfpmath=sse
#~ testconv_CFLAGS += -finput-charset=ascii
testconv_CFLAGS += -fno-asm
testconv_CFLAGS += -mfpmath=sse
testconv_CFLAGS += -pedantic
testconv_CFLAGS += $(warns) $(cwarns)

# note: clang was better at this (but i forgot why.) there was something
# the gcc version didn't warn about.
#~ testconv_sanitizer += -fsanitize=address,undefined
testconv_CFLAGS += $(testconv_sanitizer)
testconv_LIBS += $(testconv_sanitizer)
testconv_LIBS += -lm

$(testconv_OBJS): src/conv.h src/sfmt.h

.lib/testconv/%.o: %.c
	mkdir -p $(dir $@)
	$(testconv_CC) -c $< $(testconv_CFLAGS) -o $@

testconv: $(testconv_OBJS)
	$(testconv_CC) $^ $(testconv_LDFLAGS) -o $@ $(testconv_LIBS)

## ---------------------------------------------------------------------

warns += -Wall -Wextra
warns += -Wcast-qual
warns += -Wduplicated-branches
warns += -Wduplicated-cond
warns += -Werror=address
warns += -Werror=format
warns += -Werror=free-nonheap-object
warns += -Werror=init-self
warns += -Werror=maybe-uninitialized
warns += -Werror=return-local-addr
warns += -Werror=return-type
warns += -Werror=uninitialized
warns += -Werror=use-after-free
warns += -Werror=vla
warns += -Wlogical-op
warns += -Wmissing-declarations
warns += -Wno-missing-field-initializers
warns += -Wno-sign-compare
warns += -Wno-unused-parameter
warns += -Wredundant-decls
warns += -Wsuggest-attribute=format
warns += -Wsuggest-attribute=noreturn
warns += -Wundef
warns += -Wunused-macros
warns += -Wuseless-cast
warns += -Wwrite-strings

warns += -Wdouble-promotion
warns += -Wfloat-conversion

cwarns += -Wdeclaration-after-statement
cwarns += -Wno-useless-cast
cwarns += -Wc++-compat
cwarns += -Wstrict-prototypes

clangwarns = $(warns)
clangwarns := $(filter-out -Wduplicated-branches,$(clangwarns))
clangwarns := $(filter-out -Wduplicated-cond,$(clangwarns))
clangwarns := $(filter-out -Werror=maybe-uninitialized,$(clangwarns))
clangwarns := $(filter-out -Werror=use-after-free,$(clangwarns))
clangwarns := $(filter-out -Wlogical-op,$(clangwarns))
clangwarns := $(filter-out -Wsuggest-attribute=format,$(clangwarns))
clangwarns := $(filter-out -Wsuggest-attribute=noreturn,$(clangwarns))
clangwarns := $(filter-out -Wuseless-cast,$(clangwarns))

$(imgui_OBJS): warns += -Wno-useless-cast
$(imgui_OBJS): warns += -Wno-suggest-attribute=noreturn
$(imgui_OBJS): warns += -Wno-suggest-attribute=format
