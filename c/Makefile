CC      ?= gcc
CFLAGS  ?= -std=c17 -Wall -Wextra -Werror -O2 -Iinclude
LDFLAGS ?=

BUILD     := build
SRC       := src
TESTS     := tests

BIN_MAIN  := $(BUILD)/algoforge
BIN_BENCH := $(BUILD)/af_bench
BIN_TEST  := $(BUILD)/af_tests

# Library sources = everything in src/ except entry-points.
LIB_SRCS  := $(filter-out $(SRC)/main.c $(SRC)/bench.c,$(wildcard $(SRC)/*.c))
# test_risk.c embeds its own main() and links src files directly; exclude from combined suite.
TEST_SRCS := $(filter-out $(TESTS)/test_risk.c,$(wildcard $(TESTS)/*.c))

.PHONY: all bin bench test clean
all: bin

bin:   $(BIN_MAIN)
bench: $(BIN_BENCH)
test:  $(BIN_TEST)
	./$(BIN_TEST)

$(BIN_MAIN): $(SRC)/main.c $(LIB_SRCS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_BENCH): $(SRC)/bench.c $(LIB_SRCS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BIN_TEST): $(TEST_SRCS) $(LIB_SRCS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf $(BUILD)
