MAKEFLAGS += --no-builtin-rules --no-builtin-variables
.SUFFIXES:

# Define the build directory
BUILD_DIR := build
BIN := $(BUILD_DIR)/server

# Automatically discover all C++ source files in the current directory
SRCS := $(wildcard *.cpp)

# Transform source file names into object file paths inside the build directory
OBJS := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# Detect the Operating System
UNAME_S := $(shell uname -s)

# Base cross-platform flags (Include the build directory in search paths for resource generation)
CXXFLAGS := -std=c++23 -Wall -Wextra -O3 -flto -I$(BUILD_DIR)
LDFLAGS := -lusb-1.0

ifeq ($(UNAME_S),Darwin)
    # --- macOS Configuration ---
    CXX := clang++
    
    # Dynamically find Homebrew prefix (handles both Apple Silicon and Intel)
    HOMEBREW_PREFIX := $(shell brew --prefix 2>/dev/null || echo /opt/homebrew)
    
    CXXFLAGS += -I$(HOMEBREW_PREFIX)/include
    LDFLAGS += -L$(HOMEBREW_PREFIX)/lib
else
    # --- Linux / Raspberry Pi 5 Configuration ---
    CXX := g++
    
    # BCM2712 Cortex-A76 optimization
    CXXFLAGS += -mcpu=cortex-a76 -mtune=cortex-a76 -flto=auto -g -fno-omit-frame-pointer
endif

all: $(BIN)

# Include automatically generated header dependency rules
-include $(DEPS)

# Rule to translate index.html into an embedded C++ string constant header file
$(BUILD_DIR)/embedded_html.hpp: index.html | $(BUILD_DIR)
	@echo "[Asset Pipeline] Packaging index.html -> embedded_html.hpp"
	@echo "#pragma once" > $@
	@echo "#include <string_view>" >> $@
	@echo "constexpr std::string_view htmlPageContent = R\"html(" >> $@
	@cat index.html >> $@
	@echo ")html\";" >> $@

# Rule to create the build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Rule to link the final executable
$(BIN): $(OBJS) Makefile | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LDFLAGS) -o "$@"

# Rule to compile source files into object files (Ensures the asset header exists first)
$(BUILD_DIR)/%.o: %.cpp Makefile $(BUILD_DIR)/embedded_html.hpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -MMD -c "$<" -o "$@"

clean:
	rm -rf $(BUILD_DIR)
