CC := gcc
CPPFLAGS := -MMD -MP -D_FORTIFY_SOURCE=2 -D_GNU_SOURCE -DDDB_API_LEVEL=10 -DDDB_WARN_DEPRECATED=1
CFLAGS := -O2 -fstack-clash-protection -g -fPIC
LDFLAGS := -shared

ifneq (,$(LTO))
 CFLAGS += -flto
 LDFLAGS += $(CFLAGS)
endif

# ~

CFLAGS += \
	-Wall \
	-Wextra \
	-Wdeclaration-after-statement \
	-Wmissing-prototypes \
	-Wno-misleading-indentation \
	-Wno-unused-parameter \
	-Werror=format \
	-Werror=implicit-function-declaration \
	-Werror=incompatible-pointer-types \
	-Werror=int-conversion \
	-Werror=return-type \
	-Werror=uninitialized \

# ~

all: dsp_winamp.so

OBJS = \
	plugin.o \
	chldproc.o \
	fmt.o \
	chldinit.o \

chldinit.o: CFLAGS += -Os

-include $(OBJS:.o=.d)

dsp_winamp.so: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

.c.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

install:
	@cp -v dsp_winamp.so ~/.local/lib/deadbeef/

clean:
	@rm -fv -- $(OBJS:.o=.d) $(OBJS) dsp_winamp.so

watch:
	@while ls $(OBJS:.o=.c) $$(cat $(OBJS:.o=.d) | sed -E 's/ [^ ]*\\ ([^ ]|\\ )+ / /g; s/^([^: ]|\\ )+://; /^$$/d; s/\\$$//') | awk '!t[$$0]++' | entr -cs '$(MAKE) && size dsp_winamp.so'; do\
		continue;\
	done
