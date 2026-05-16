CC      ?= gcc
CFLAGS  ?= -std=c17 -Wall -Wextra -Werror -O2 -Iinclude
LDFLAGS ?=

BUILD   := build
SRC     := src
TESTS   := tests
BIN     := $(BUILD)/algoforge
TESTBIN := $(BUILD)/af_tests

SRCS  := $(wildcard $(SRC)/*.c)
TSRCS := $(filter-out $(SRC)/main.c,$(SRCS)) $(wildcard $(TESTS)/*.c)

.PHONY: all bin test clean
all: bin

bin: $(BIN)

$(BIN): $(SRCS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(TESTBIN): $(TSRCS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: $(TESTBIN)
	./$(TESTBIN)

clean:
	rm -rf $(BUILD)
