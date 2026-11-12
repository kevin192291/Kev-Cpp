---
description: A Makefile that builds every .cpp file under src/ into build/, tracking header dependencies itself
file: Makefile
whole-file: yes
ask: Program = app
order: 11
section: starters
---
# `make` builds build/release/{{Program}}; `make DEBUG=1` builds build/debug/{{Program}} for the debugger.
# `make run ARGS="..."` builds and runs it. -MMD -MP write a .d file beside each object listing the
# headers it includes, so editing a header rebuilds whatever includes it.
TARGET  := {{Program}}
SRC_DIR := src

CXX      := g++
CPPFLAGS :=
CXXFLAGS := -std=c++20 -Wall -Wextra
LDFLAGS  :=
LDLIBS   :=

ifeq ($(DEBUG),1)
    BUILD_DIR := build/debug
    CXXFLAGS  += -O0 -g
else
    BUILD_DIR := build/release
    CXXFLAGS  += -O2 -DNDEBUG
endif

SRCS := $(shell find $(SRC_DIR) -name '*.cpp')
OBJS := $(SRCS:$(SRC_DIR)/%.cpp=$(BUILD_DIR)/obj/%.o)
DEPS := $(OBJS:.o=.d)

.PHONY: all run clean

all: $(BUILD_DIR)/$(TARGET)

$(BUILD_DIR)/$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(BUILD_DIR)/obj/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

run: all
	./$(BUILD_DIR)/$(TARGET) $(ARGS)

clean:
	rm -rf build

-include $(DEPS)
