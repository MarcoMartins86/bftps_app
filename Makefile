.PHONY: all linux 3ds clean

all:
	@echo please choose 3ds, linux
3ds: 
	@$(MAKE) -C bftps -f Makefile 3ds
	@$(MAKE) -f Makefile.3ds
linux:
	@$(MAKE) -C bftps -f Makefile linux 
	@$(MAKE) -f Makefile.linux
clean:
	@$(MAKE) -C bftps -f Makefile	clean
	@$(MAKE) -f Makefile.3ds 	clean
	@$(MAKE) -f Makefile.linux	clean
