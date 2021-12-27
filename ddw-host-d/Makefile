build: buildcoff
release: releasecoff

buildcoff:
	@wine dub build -q --arch=x86_mscoff --build-mode=allAtOnce && exec size ddw-host-d.exe
buildomf:
	@wine dub build -q --arch=x86 --build-mode=allAtOnce && exec size ddw-host-d.exe
lbuild:
	@dub build -q --arch=x86 --build-mode=allAtOnce && exec size ddw-host-d

test unittest:
	@wine dub build --arch=x86_mscoff -b unittest && wine ./ddw-host-d.exe
ltest lunittest:
	@dub build --arch=x86 -b unittest && ./ddw-host-d

watch:
	find src/ -name '*.[cd]' | entr -cr ttl make -s build
lwatch:
	find src/ -name '*.[cd]' | entr -cr ttl make -s lbuild

releasecoff:
	@DFLAGS='-O -L=/opt:ref -g -mcpu=avx' wine dub build -q --arch=x86_mscoff --build-mode=allAtOnce && exec size ddw-host-d.exe
releaseomf:
	@DFLAGS='-O -g -mcpu=avx' wine dub build -q --arch=x86 --build-mode=allAtOnce && exec size ddw-host-d.exe
lrelease:
	@DFLAGS='-O -L=--gc-sections -g -mcpu=avx' dub build -q --arch=x86 --build-mode=allAtOnce && exec size ddw-host-d

install:
	@cp -v ddw-host-d.exe ~/.local/bin/ddw-host-d.exe
