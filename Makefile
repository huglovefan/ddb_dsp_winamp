DIRS := ddw-d ddw-host-d ddw-shm-d

releaseall:
	@for d in $(DIRS); do ( cd $$d && make release ); done

installall:
	@for d in $(DIRS); do ( cd $$d && make install ); done

all: releaseall installall
