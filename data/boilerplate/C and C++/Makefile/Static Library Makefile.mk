---
description: A Makefile that builds a static library (.a) from the .c files under src/, with headers in include/
file: Makefile
whole-file: yes
ask: Name = mylib
order: 12
section: starters
---
# `make` builds build/lib{{Name}}.a. A program uses it with -Ipath/include, and links with
# -Lpath/build -l{{Name}}.
LIB       := lib{{Name}}.a
SRC_DIR   := src
BUILD_DIR := build

CC       := gcc
CPPFLAGS := -Iinclude
CFLAGS   := -std=c17 -Wall -Wextra -O2
ARFLAGS  := rcs

SRCS := $(shell find $(SRC_DIR) -name '*.c')
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/obj/%.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all clean

all: $(BUILD_DIR)/$(LIB)

$(BUILD_DIR)/$(LIB): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(BUILD_DIR)/obj/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)

-include $(DEPS)
