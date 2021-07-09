CPPFLAGS := -D_FORTIFY_SOURCE=2 -DDDB_API_LEVEL=10 -DDDB_WARN_DEPRECATED=1
CFLAGS := -O2 -fstack-clash-protection -g -fPIC
LDFLAGS := -shared

CFLAGS += \
	-Wall \
	-Wextra \
	-Wmissing-prototypes \
	-Wno-misleading-indentation \
	-Wno-unused-parameter \
	-Werror=format \
	-Werror=implicit-function-declaration \
	-Werror=incompatible-pointer-types \
	-Werror=int-conversion \
	-Werror=return-type \
	-Werror=uninitialized \

dsp_winamp.so: plugin.c
	$(CC) $(CPPFLAGS) $(LDFLAGS) $(CFLAGS) plugin.c -o $@ -lkqueue

install:
	@cp -v dsp_winamp.so ~/.local/lib/deadbeef/

clean:
	@rm -fv dsp_winamp.so

watch:
	ls plugin.c ddw.h misc.h | entr -cs 'make && size dsp_winamp.so'
