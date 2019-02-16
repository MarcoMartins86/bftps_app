.PHONY: all linux 3ds clean

all:
	@echo please choose 3ds_release, 3ds_debug, linux_release, linux_debug
3ds_release: 
	@$(MAKE) -f Makefile.3ds
3ds_debug: 
	@$(MAKE) -f Makefile.3ds BUILD=debug
linux_release:
	@$(MAKE) -f Makefile.linux
linux_debug:
	@$(MAKE) -f Makefile.linux BUILD=debug
clean:
	@$(MAKE) -f Makefile.3ds 	clean
	@$(MAKE) -f Makefile.linux	clean
