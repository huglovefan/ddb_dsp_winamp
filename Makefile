SUBDIRS := host plugin

.PHONY: all $(SUBDIRS) install uninstall clean

all: $(SUBDIRS)

$(SUBDIRS):
	$(MAKE) -C $@

install uninstall clean:
	$(MAKE) -C host $@
	$(MAKE) -C plugin $@
