# Makefile for project using VeriSimplePIR with CSV
# Multi OS makefile (macOS and Linux)

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
PKG_CONFIG ?= pkg-config

# ============================================================================
# Paths to VeriSimplePIR (relative to project)
# ============================================================================
VERISIMPLEPIR_DIR := $(shell pwd)/VeriSimplePIR
VERISIMPLEPIR_LIB := $(VERISIMPLEPIR_DIR)/bin/lib/libverisimplepir
VERISIMPLEPIR_INC := $(VERISIMPLEPIR_DIR)/src/lib

# ============================================================================
# Compiler configuration (portable)
# ============================================================================
CC ?= clang++
CPPFLAGS += -std=c++17 -O3 -Wall -fno-omit-frame-pointer
# Reduce warning noise
CPPFLAGS += -Wno-unused-variable -Wno-unused-parameter -Wno-unused-const-variable -Wno-unused-local-typedef -Wno-deprecated-declarations

# SSE4 / AES only on x86_64
ifeq ($(UNAME_M),x86_64)
    CPPFLAGS += -maes -msse4
endif

# ============================================================================
# OpenSSL via pkg-config (fallback to -lssl -lcrypto)
# ============================================================================
OPENSSL_CFLAGS := $(shell $(PKG_CONFIG) --cflags openssl 2>/dev/null)
OPENSSL_LIBS   := $(shell $(PKG_CONFIG) --libs openssl 2>/dev/null)
CPPFLAGS += $(OPENSSL_CFLAGS) -I$(VERISIMPLEPIR_INC)
LDFLAGS  += $(if $(OPENSSL_LIBS),$(OPENSSL_LIBS),-lssl -lcrypto)

# ============================================================================
# Parquet support (optional, enabled if PARQUET_SUPPORT=1 and pkg-config ok)
# ============================================================================
PARQUET_SUPPORT ?= 1
ifeq ($(PARQUET_SUPPORT),1)
    ARROW_CFLAGS  := $(shell $(PKG_CONFIG) --cflags arrow parquet 2>/dev/null)
    ARROW_LIBS    := $(shell $(PKG_CONFIG) --libs arrow parquet 2>/dev/null)
    ifneq ($(ARROW_CFLAGS)$(ARROW_LIBS),)
        CPPFLAGS += -DPARQUET_SUPPORT $(ARROW_CFLAGS)
        LDFLAGS  += $(ARROW_LIBS)
    else
        $(warning PARQUET_SUPPORT=1 but Arrow/Parquet not found via pkg-config)
    endif
endif

# ============================================================================
# VeriSimplePIR library
# ============================================================================
LDFLAGS += -L$(VERISIMPLEPIR_DIR)/bin/lib -Wl,-rpath,$(VERISIMPLEPIR_DIR)/bin/lib -lverisimplepir
ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -lc++
else
    LDFLAGS += -lstdc++ -lm
endif
LIBSUFFIX := $(if $(filter $(UNAME_S),Darwin),.dylib,.so)
EXESUFFIX :=

# ============================================================================
# Project structure
# ============================================================================
SRCDIR := src
INCDIR := include
BUILDDIR := build
BINDIR := bin

# Project sources
SOURCES := $(wildcard $(SRCDIR)/*.cpp)
OBJECTS := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SOURCES))
DEPENDS := $(OBJECTS:.o=.d)

# Executable name
TARGET := $(BINDIR)/pir

# ============================================================================
# ANSI color codes
# ============================================================================
COLOR_RESET := \033[0m
COLOR_GREEN := \033[0;32m
COLOR_YELLOW := \033[0;33m
COLOR_BLUE := \033[0;34m
COLOR_CYAN := \033[0;36m
COLOR_BOLD := \033[1m

# ============================================================================
# Main rules
# ============================================================================
.PHONY: all clean directories verisimplepir

all: verisimplepir directories $(TARGET)

# Create necessary directories
directories:
	@mkdir -p $(BUILDDIR)
	@mkdir -p $(BINDIR)

# Link the executable
$(TARGET): $(OBJECTS) $(VERISIMPLEPIR_LIB)$(LIBSUFFIX)
	@echo "$(COLOR_CYAN)Linking $(TARGET)...$(COLOR_RESET)"
	@$(CC) -o $@ $(OBJECTS) $(LDFLAGS) 2>&1 | grep -vE "(warning:|note:)" || true
ifeq ($(UNAME_S),Darwin)
	@echo "$(COLOR_CYAN)Fixing library path...$(COLOR_RESET)"
	@install_name_tool -change bin/lib/libverisimplepir.dylib $(VERISIMPLEPIR_DIR)/bin/lib/libverisimplepir.dylib $@ 2>/dev/null || true
endif
	@echo "$(COLOR_GREEN)✓ Build complete: $(TARGET)$(COLOR_RESET)"

# Compile source files
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp
	@echo "$(COLOR_BLUE)Compiling $(COLOR_BOLD)$<$(COLOR_RESET)$(COLOR_BLUE)...$(COLOR_RESET)"
	@mkdir -p $(@D)
	@$(CC) $(CPPFLAGS) -I$(INCDIR) -MM -MT $@ $< > $(BUILDDIR)/$*.d 2>/dev/null
	@$(CC) $(CPPFLAGS) -I$(INCDIR) -c -o $@ $< 2>&1 | grep -vE "(warning:|generated|note:|^[[:space:]]*[0-9]+ warnings generated)" || true

# Compile VeriSimplePIR if necessary
verisimplepir: $(VERISIMPLEPIR_LIB)$(LIBSUFFIX)

# Check and compile VeriSimplePIR library if it doesn't exist
$(VERISIMPLEPIR_LIB)$(LIBSUFFIX):
	@echo "$(COLOR_CYAN)Checking VeriSimplePIR library...$(COLOR_RESET)"
	@LIB_PATH="$(VERISIMPLEPIR_LIB)$(LIBSUFFIX)"; \
	if [ ! -f "$$LIB_PATH" ]; then \
		echo "$(COLOR_YELLOW)VeriSimplePIR library not found. Building VeriSimplePIR...$(COLOR_RESET)"; \
		if [ ! -d "$(VERISIMPLEPIR_DIR)" ]; then \
			echo "$(COLOR_BOLD)Error: VeriSimplePIR directory not found at $(VERISIMPLEPIR_DIR)$(COLOR_RESET)"; \
			echo "Please ensure VeriSimplePIR is present in the project directory"; \
			exit 1; \
		fi; \
		cd $(VERISIMPLEPIR_DIR) && \
		unset LDFLAGS CPPFLAGS CFLAGS CXXFLAGS && \
		$(MAKE) -s > /tmp/vspir_build.log 2>&1; \
		build_exit=$$?; \
		if [ $$build_exit -ne 0 ]; then \
			echo "$(COLOR_BOLD)Build failed. Showing errors:$(COLOR_RESET)"; \
			grep -E "(error|Error|ERROR|failed|Failed)" /tmp/vspir_build.log | head -20 || tail -30 /tmp/vspir_build.log; \
			rm -f /tmp/vspir_build.log; \
			exit $$build_exit; \
		fi; \
		rm -f /tmp/vspir_build.log; \
		cd - > /dev/null; \
		if [ ! -f "$$LIB_PATH" ]; then \
			echo "$(COLOR_BOLD)Error: Failed to build VeriSimplePIR library$(COLOR_RESET)"; \
			exit 1; \
		fi; \
		echo "$(COLOR_GREEN)✓ VeriSimplePIR library built successfully$(COLOR_RESET)"; \
	else \
		echo "$(COLOR_GREEN)✓ VeriSimplePIR library found$(COLOR_RESET)"; \
	fi

# Include dependencies
-include $(DEPENDS)

# Cleanup
clean:
	@echo "$(COLOR_YELLOW)Cleaning...$(COLOR_RESET)"
	@$(RM) -rf $(BUILDDIR) $(BINDIR)
	@echo "$(COLOR_GREEN)✓ Clean complete$(COLOR_RESET)"

# Also clean VeriSimplePIR
clean-all: clean
	@echo "$(COLOR_YELLOW)Cleaning VeriSimplePIR...$(COLOR_RESET)"
	@if [ -d "$(VERISIMPLEPIR_DIR)" ]; then \
		cd $(VERISIMPLEPIR_DIR) && $(MAKE) clean && cd -; \
	fi
	@echo "$(COLOR_GREEN)✓ Clean-all complete$(COLOR_RESET)"

# Help
help:
	@echo "$(COLOR_BOLD)Makefile for PIR project with CSV$(COLOR_RESET)"
	@echo ""
	@echo "$(COLOR_CYAN)Available targets:$(COLOR_RESET)"
	@echo "  $(COLOR_GREEN)make$(COLOR_RESET)          - Build the project (compiles VeriSimplePIR if necessary)"
	@echo "  $(COLOR_GREEN)make verisimplepir$(COLOR_RESET) - Build only VeriSimplePIR"
	@echo "  $(COLOR_GREEN)make clean$(COLOR_RESET)    - Clean generated project files"
	@echo "  $(COLOR_GREEN)make clean-all$(COLOR_RESET) - Clean project and VeriSimplePIR"
	@echo "  $(COLOR_GREEN)make help$(COLOR_RESET)     - Show this help"
	@echo ""
	@echo "$(COLOR_CYAN)Configuration:$(COLOR_RESET)"
	@echo "  VeriSimplePIR: $(COLOR_BOLD)$(VERISIMPLEPIR_DIR)$(COLOR_RESET)"
	@echo "  Compilateur:   $(COLOR_BOLD)$(CC)$(COLOR_RESET)"
	@echo "  Système:       $(COLOR_BOLD)$(UNAME_S) ($(UNAME_M))$(COLOR_RESET)"