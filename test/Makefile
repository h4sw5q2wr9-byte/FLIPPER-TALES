# Host build for the pure-C99 core in src/core.
#
# Nothing here touches the Flipper SDK: the core is deliberately free of
# hardware dependencies so the combat maths can be tested before flashing.

CORE_DIR := ../src/core
BIN      := bin

CFLAGS := -std=c99 -Wall -Wextra -Werror -Wpedantic -Wshadow \
          -Wconversion -Wsign-conversion -O2 -g -I$(CORE_DIR)

APP_DIR  := ../src/app
CORE_SRC := $(wildcard $(CORE_DIR)/*.c)

# The preview harness compiles the real renderer against a stub canvas, so the
# 128x64 layout can be inspected (and clipping caught) without a device.
PREVIEW_CFLAGS := -std=c99 -Wall -Wextra -Werror -Wpedantic -O2 -g \
                  -I$(CORE_DIR) -I$(APP_DIR) -Istub -I.

.PHONY: all test sim preview map check-nofloat clean

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

$(BIN)/preview: $(CORE_SRC) $(APP_DIR)/ft_render.c canvas_stub.c preview.c | $(BIN)
	$(CC) $(PREVIEW_CFLAGS) $^ -o $@

$(BIN)/map_preview: $(CORE_SRC) $(APP_DIR)/ft_overworld.c canvas_stub.c map_preview.c | $(BIN)
	$(CC) $(PREVIEW_CFLAGS) $^ -o $@

map: $(BIN)/map_preview
	@mkdir -p preview
	@rm -f preview/map_*.pbm preview/whole_*.pbm
	@./$(BIN)/map_preview
	@python3 topng.py map

preview: $(BIN)/preview
	@mkdir -p preview
	@rm -f preview/[0-9]*.pbm preview/9*_help*.pbm
	@./$(BIN)/preview
	@python3 topng.py

# The MCU has no FPU budget, so the core must stay integer-only.
check-nofloat:
	@if grep -nE '\b(float|double)\b' $(CORE_DIR)/*.c $(CORE_DIR)/*.h; then \
		echo "ERROR: floating point found in src/core (see DESIGN.md 2.2)"; \
		exit 1; \
	fi
	@echo "no floating point in src/core"

clean:
	@rm -rf $(BIN) preview
