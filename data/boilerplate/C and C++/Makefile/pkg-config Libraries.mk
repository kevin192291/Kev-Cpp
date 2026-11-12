---
description: Compiler and linker flags for pkg-config libraries (gtk4 here), with a clear error when one is missing
ask: Packages = gtk4
order: 21
section: parts
---
# Goes after CPPFLAGS and LDLIBS are first set. Several names can go in PKGS, separated by spaces;
# `pkg-config --list-all` shows what is installed.
PKGS := {{Packages}}
ifneq ($(shell pkg-config --exists $(PKGS) && echo yes),yes)
    $(error pkg-config can't find $(PKGS): install the development package)
endif
CPPFLAGS += $(shell pkg-config --cflags $(PKGS))
LDLIBS   += $(shell pkg-config --libs $(PKGS))
