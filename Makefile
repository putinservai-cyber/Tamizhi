CC      ?= cc
BUILD   := build
BIN     := $(BUILD)/ta
LIB     := $(BUILD)/tart.o

WARN    := -std=c11 -Wall -Wextra -Wpedantic -Werror
CFLAGS  ?= -O2
INC     := -Iinclude

COMPILER_SRCS := $(wildcard src/common/*.c src/lexer/*.c src/parser/*.c src/ast/*.c src/semantic/*.c src/typecheck/*.c src/ir/*.c src/codegen/*.c src/cli/*.c)
COMPILER_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(COMPILER_SRCS))

UNIT_SRCS  := $(wildcard tests/unit/test_*.c)
UNIT_BINS  := $(patsubst tests/unit/%.c,$(BUILD)/tests/%,$(UNIT_SRCS))

PREFIX ?= $(HOME)/.local

.PHONY: all test clean install uninstall

$(BUILD)/tai: $(BIN)
	@mkdir -p $(dir $@)
	@printf '#!/bin/sh\nexec "%s" run "$$@"\n' "$(abspath $(BUILD)/ta)" > $@
	@chmod +x $@

install: all
	install -Dm755 $(BIN) $(PREFIX)/bin/ta
	install -Dm644 $(LIB) $(PREFIX)/lib/tamizhi/tart.o
	@printf '#!/bin/sh\nexec "%s/bin/ta" run "$$@"\n' "$(PREFIX)" > $(PREFIX)/bin/tai
	@chmod +x $(PREFIX)/bin/tai
	@echo "installed: $(PREFIX)/bin/{ta,tai} $(PREFIX)/lib/tamizhi/tart.o"

uninstall:
	rm -f $(PREFIX)/bin/ta $(PREFIX)/bin/tai $(PREFIX)/lib/tamizhi/tart.o
	-rmdir $(PREFIX)/lib/tamizhi 2>/dev/null || true

all: $(BIN) $(LIB) $(BUILD)/tai

$(BIN): $(COMPILER_OBJS)
	$(CC) $(CFLAGS) $(WARN) -o $@ $^

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARN) $(INC) -c -o $@ $<

$(LIB): src/runtime/tart.c include/tart.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARN) $(INC) -c -o $@ $<

$(BUILD)/tests/%: tests/unit/%.c $(filter-out $(BUILD)/src/cli/main.o,$(COMPILER_OBJS))
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(WARN) $(INC) -o $@ $< $(filter-out $(BUILD)/src/cli/main.o,$(COMPILER_OBJS))

test: all $(UNIT_BINS)
	@for t in $(UNIT_BINS); do ./$$t || exit 1; done
	@bash tests/run_e2e.sh

clean:
	rm -rf $(BUILD)

.PHONY: install uninstall
