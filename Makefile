# Compiler
CC := gcc

# Compiler Flags
CFLAGS := -Wall -Wextra -std=c11 -g

# Directories
SRC_DIR := src
BUILD_DIR := build

# Source Files and Headers
SRCS := $(wildcard $(SRC_DIR)/*.c)
HEADERS := $(wildcard $(SRC_DIR)/*.h)

# Object Files (place in build/)
OBJS := $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRCS))

# Output Executable
TARGET := $(BUILD_DIR)/lang

# Default Target
all: $(TARGET)

# Link Object Files to Create the Executable
$(TARGET): $(OBJS)
	@echo "Linking $(TARGET)..."
	$(CC) $(CFLAGS) -o $@ $^

# Compile Source Files into Object Files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS)
	@mkdir -p $(BUILD_DIR)
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

# Clean Up Generated Files
clean:
	@echo "Cleaning up..."
	rm -rf $(BUILD_DIR)

# Rebuild Everything
rebuild: clean all

.PHONY: all clean rebuild
