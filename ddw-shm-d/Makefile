build:
	@dub build -q --build-mode=allAtOnce && exec size libddw-shm-d.so

test unittest:
	dub test

# manual changes:
# - unwrap this typedef: typedef struct DB_output_s DB_output_t;
#   ^ for this bug: https://issues.dlang.org/show_bug.cgi?id=22625
src/zzz_deadbeef.c:
	cpp -P -std=c11 -DDDB_API_LEVEL=10 -D__asm__\(x\)= -D__restrict= -include deadbeef/deadbeef.h /dev/null >$@

watch:
	find src/ -name '*.[cd]' | entr -cr ttl make -s

release:
	@DFLAGS='-L=--gc-sections -L=--hash-style=gnu -O -checkaction=C -g -mcpu=avx' dub build -q --build-mode=allAtOnce && exec size libddw-shm-d.so

install:
	@cp -v libddw-shm-d.so ~/.local/lib/deadbeef/ddb_shm.so
