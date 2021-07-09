CC := gcc
CPPFLAGS := -MMD -MP -pthread -D_GNU_SOURCE -D_FORTIFY_SOURCE=2
CFLAGS := -O2 -g -fstack-protector-strong -fPIC
LDFLAGS := -shared
LDLIBS := -pthread -lkqueue

# ~

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

# ~

all: ddb_shm.so

OBJS = \
	plugin.o \
	tickmain.o \
	shm.o \

shm.o: CFLAGS += -Os

-include $(OBJS:.o=.d)

ddb_shm.so: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@ $(LDLIBS)

.c.o:
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

install:
	@cp -v ddb_shm.so ~/.local/lib/deadbeef/

clean:
	@rm -fv -- $(OBJS:.o=.d) $(OBJS) ddb_shm.so

watch:
	@while ls $(OBJS:.o=.c) $$(cat $(OBJS:.o=.d) | sed -E 's/ [^ ]*\\ ([^ ]|\\ )+ / /g; s/^([^: ]|\\ )+://; /^$$/d; s/\\$$//') | awk '!t[$$0]++' | entr -cs '$(MAKE) && size ddb_shm.so'; do\
		continue;\
	done
