libname := ddw-d
installdir := ~/.local/lib/deadbeef
installfilename := dsp_winamp.so
include ../../dbase.mak

# manual changes:
# - unwrap this typedef: typedef struct DB_output_s DB_output_t;
#   ^ for this bug: https://issues.dlang.org/show_bug.cgi?id=22625
src/zzz_deadbeef.c:
	cpp -P -std=c11 -DDDB_API_LEVEL=10 -D__asm__\(x\)= -D__restrict= -include deadbeef/deadbeef.h /dev/null >$@
