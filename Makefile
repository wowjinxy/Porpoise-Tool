# Makefile for Porpoise Tool
# PowerPC to C Transpiler for GameCube/Wii

CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -Iinclude -O2
LDFLAGS = 

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
BIN_DIR = bin

# Source files
PORPOISE_SRC = $(SRC_DIR)/porpoise_tool.c
EXAMPLE_SRCS = $(SRC_DIR)/transpiler_example.c \
               $(SRC_DIR)/gecko_memory_example.c \
               $(SRC_DIR)/gecko_memory_example_simplified.c

# Output binaries
PORPOISE_BIN = $(BIN_DIR)/porpoise_tool
EXAMPLE_BINS = $(BIN_DIR)/transpiler_example \
               $(BIN_DIR)/gecko_memory_example \
               $(BIN_DIR)/gecko_memory_example_simplified

# Windows executable extension
ifeq ($(OS),Windows_NT)
    PORPOISE_BIN := $(PORPOISE_BIN).exe
    EXAMPLE_BINS := $(addsuffix .exe,$(EXAMPLE_BINS))
endif

# Default target
all: $(BIN_DIR) porpoise

# Create directories
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build Porpoise Tool
porpoise: $(BIN_DIR) $(PORPOISE_SRC)
	$(CC) $(CFLAGS) -o $(PORPOISE_BIN) $(PORPOISE_SRC) $(LDFLAGS)
	@echo "Built: $(PORPOISE_BIN)"

# Build examples
examples: $(BIN_DIR) $(EXAMPLE_BINS)

$(BIN_DIR)/transpiler_example: $(SRC_DIR)/transpiler_example.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BIN_DIR)/gecko_memory_example: $(SRC_DIR)/gecko_memory_example.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BIN_DIR)/gecko_memory_example_simplified: $(SRC_DIR)/gecko_memory_example_simplified.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Windows executable variants
$(BIN_DIR)/transpiler_example.exe: $(SRC_DIR)/transpiler_example.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BIN_DIR)/gecko_memory_example.exe: $(SRC_DIR)/gecko_memory_example.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(BIN_DIR)/gecko_memory_example_simplified.exe: $(SRC_DIR)/gecko_memory_example_simplified.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# Clean build artifacts
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)
	@echo "Cleaned build artifacts"

# Run Porpoise Tool
run: porpoise
	@echo "Run with: $(PORPOISE_BIN) <directory> [skip_list.txt]"

# Display help
help:
	@echo "Porpoise Tool - Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build Porpoise Tool (default)"
	@echo "  porpoise  - Build Porpoise Tool transpiler"
	@echo "  examples  - Build example programs"
	@echo "  clean     - Remove build artifacts"
	@echo "  run       - Display run instructions"
	@echo "  help      - Display this help message"
	@echo ""
	@echo "Usage:"
	@echo "  make                    # Build Porpoise Tool"
	@echo "  make examples           # Build examples"
	@echo "  make clean              # Clean"
	@echo ""
	@echo "Running Porpoise Tool:"
	@echo "  $(PORPOISE_BIN) <asm_dir> [skip_list.txt]"

.PHONY: all porpoise examples clean run help

