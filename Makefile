all:
	#$(MAKE) -C host
	$(MAKE) -C plugin
	$(MAKE) -C shm

install:
	$(MAKE) -C host install
	$(MAKE) -C plugin install
	$(MAKE) -C shm install

clean:
	$(MAKE) -C host clean
	$(MAKE) -C plugin clean
	$(MAKE) -C shm clean
