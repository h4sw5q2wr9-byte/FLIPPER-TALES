# Host build for the pure-C99 core in src/core.
#
# Nothing here touches the Flipper SDK: the core is deliberately free of
# hardware dependencies so the combat maths can be tested before flashing.

CORE_DIR := ../src/core
BIN      := bin

CFLAGS := -std=c99 -Wall -Wextra -Werror -Wpedantic -Wshadow \
          -Wconversion -Wsign-conversion -O2 -g -I$(CORE_DIR)

CORE_SRC := $(wildcard $(CORE_DIR)/*.c)

.PHONY: all test sim check-nofloat clean

all: test

$(BIN):
	@mkdir -p $(BIN)

$(BIN)/test_main: $(CORE_SRC) test_main.c | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@

$(BIN)/sim: $(CORE_SRC) sim.c | $(BIN)
	$(CC) $(CFLAGS) $^ -o $@

test: $(BIN)/test_main check-nofloat
	@./$(BIN)/test_main

sim: $(BIN)/sim
	@./$(BIN)/sim

# The MCU has no FPU budget, so the core must stay integer-only.
check-nofloat:
	@if grep -nE '\b(float|double)\b' $(CORE_DIR)/*.c $(CORE_DIR)/*.h; then \
		echo "ERROR: floating point found in src/core (see DESIGN.md 2.2)"; \
		exit 1; \
	fi
	@echo "no floating point in src/core"

clean:
	@rm -rf $(BIN)
