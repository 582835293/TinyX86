# =============================================================================
# TinyX86 Cross-Platform Makefile (Linux / macOS / Windows MinGW)
# =============================================================================

CC       ?= cc
CFLAGS   ?= -std=c11 -Wall -Wextra -Wno-unused-parameter -O2 -Iinclude
LDFLAGS  ?=
LDLIBS   ?= -lm

ifeq ($(OS),Windows_NT)
    EXE_EXT := .exe
    LDLIBS  += -lpsapi
else
    EXE_EXT :=
endif

BUILD_DIR := build/make
TARGET    := $(BUILD_DIR)/TinyX86$(EXE_EXT)
TEST_BIN  := $(BUILD_DIR)/cpu_integration_tests$(EXE_EXT)

CORE_SRCS := core/TinyX86.c core/disasm.c core/PETools.c
APP_SRCS  := core/main.c core/PELoader.c core/debugger.c
TEST_SRCS := tests/cpu_integration_tests.c

.PHONY: all debug release test run web clean help

all: $(TARGET) $(TEST_BIN)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(CORE_SRCS) $(APP_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CORE_SRCS) $(APP_SRCS) -o $(TARGET) $(LDFLAGS) $(LDLIBS)

$(TEST_BIN): $(CORE_SRCS) $(TEST_SRCS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CORE_SRCS) $(TEST_SRCS) -o $(TEST_BIN) $(LDFLAGS) $(LDLIBS)

debug: CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -g -O0 -DDEBUG -Iinclude
debug: clean all

release: CFLAGS := -std=c11 -Wall -Wextra -Wno-unused-parameter -O3 -DNDEBUG -Iinclude
release: clean all

test: $(TEST_BIN)
	./$(TEST_BIN)

run: $(TARGET)
	./$(TARGET)

web:
	npm install && npm run dev

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "TinyX86 Build Targets:"
	@echo "  make          - Build TinyX86 CLI and cpu_integration_tests"
	@echo "  make debug    - Build with debug symbols (-g -O0)"
	@echo "  make release  - Build with optimizations (-O3)"
	@echo "  make test     - Build and run C integration tests"
	@echo "  make run      - Build and launch interactive TinyX86 Shell"
	@echo "  make web      - Launch interactive Web Debugger on port 3000"
	@echo "  make clean    - Remove build artifacts"
