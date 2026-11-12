---
description: debug, release, rebuild, run, clean and help targets for a Makefile with TARGET, BUILD_DIR and DEBUG
order: 20
section: parts
---
# For a Makefile that sets BUILD_DIR and its flags from DEBUG, as the Program Makefiles here do; leave
# out any target it already has.
# .PHONY marks names that aren't files, so a file called "clean" can't stop `make clean`.
.PHONY: debug release rebuild run clean help

debug:
	$(MAKE) DEBUG=1

release:
	$(MAKE) DEBUG=0

rebuild: clean
	$(MAKE)

run: all
	./$(BUILD_DIR)/$(TARGET) $(ARGS)

clean:
	rm -rf build

help:
	@echo "make                 release build"
	@echo "make debug           debug build, for the debugger"
	@echo "make rebuild         clean, then build"
	@echo "make run ARGS=\"...\"  build and run"
	@echo "make clean           delete build/"
