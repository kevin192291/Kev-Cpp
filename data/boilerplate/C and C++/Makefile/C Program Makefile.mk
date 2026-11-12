---
description: A Makefile that builds every .c file under src/ into build/, tracking header dependencies itself
file: Makefile
whole-file: yes
ask: Program = app
order: 10
section: starters
---
# `make` builds build/release/{{Program}}; `make DEBUG=1` builds build/debug/{{Program}} for the debugger.
# `make run ARGS="..."` builds and runs it. -MMD -MP write a .d file beside each object listing the
# headers it includes, so editing a header rebuilds whatever includes it.
TARGET  := {{Program}}
SRC_DIR := src

CC       := gcc
CPPFLAGS :=
CFLAGS   := -std=c17 -Wall -Wextra
LDFLAGS  :=
LDLIBS   := -lm

ifeq ($(DEBUG),1)
    BUILD_DIR := build/debug
    CFLAGS    += -O0 -g
else
    BUILD_DIR := build/release
    CFLAGS    += -O2 -DNDEBUG
endif

SRCS := $(shell find $(SRC_DIR) -name '*.c')
OBJS := $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/obj/%.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all run clean

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/obj/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

run: all
	./$(BUILD_DIR)/$(TARGET) $(ARGS)

clean:
	rm -rf build

-include $(DEPS)
