CC := i686-w64-mingw32-gcc -m32
CPPFLAGS := -MMD -MP -I../Winamp\ SDK
CFLAGS := -O2 -g -fstack-protector-strong
LDFLAGS := -Wl,--stack,$$((16*1024*1024))
LDLIBS := -lntdll -lssp

ifneq (,$(D))
 CPPFLAGS += -DD
endif

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
	-Werror=format \
	-Werror=implicit-function-declaration \
	-Werror=incompatible-pointer-types \
	-Werror=int-conversion \
	-Werror=return-type \
	-Werror=uninitialized \

# ~

all: ddw_host.exe

OBJS = \
	procmain.o \
	plugproc.o \
	buf.o \
	fmt.o \
	misc.o \
	wndproc.o \
	shm.o \
	main.o \
	plugload.o \

shm.o: CFLAGS += -Os
plugload.o: CFLAGS += -Os

-include $(OBJS:.o=.d)

ddw_host.exe: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

.c.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

install:
	@echo; \
	echo "To install ddw_host.exe, set \"Host command\" in the deadbeef plugin configuration to the correct value:"; \
	case $$PWD in \
	"$$HOME"/*) echo "  wine ~/$${PWD#"$$HOME"/}/ddw_host.exe";; \
	*) echo "  wine $$PWD/ddw_host.exe";; \
	esac; \
	echo

clean:
	@rm -fv -- $(OBJS:.o=.d) $(OBJS) ddw_host.exe

watch:
	@while ls $(OBJS:.o=.c) $$(cat $(OBJS:.o=.d) | sed -E 's/ [^ ]*\\ ([^ ]|\\ )+ / /g; s/^([^: ]|\\ )+://; /^$$/d; s/\\$$//') | awk '!t[$$0]++' | entr -cs '$(MAKE) && size ddw_host.exe'; do\
		continue;\
	done
