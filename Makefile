DIRS := ddw-d ddw-host-d ddw-shm-d

testall:
	@for d in $(DIRS); do ( cd $$d && make test ) || exit; done

releaseall:
	@for d in $(DIRS); do ( cd $$d && make release ) || exit; done

installall:
	@for d in $(DIRS); do ( cd $$d && make install ) || exit; done

all: testall releaseall installall
