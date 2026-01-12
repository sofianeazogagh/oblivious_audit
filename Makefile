# Makefile pour projet utilisant VeriSimplePIR avec CSV
# Multi OS makefile (macOS et Linux)

# ============================================================================
# Configuration système
# ============================================================================
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
HOST_SYSTEM = $(shell uname | cut -f 1 -d_)
SYSTEM ?= $(HOST_SYSTEM)

# ============================================================================
# Chemins vers VeriSimplePIR (à adapter selon votre installation)
# ============================================================================
VERISIMPLEPIR_DIR := $(shell pwd)/VeriSimplePIR
VERISIMPLEPIR_LIB := $(VERISIMPLEPIR_DIR)/bin/lib/libverisimplepir
VERISIMPLEPIR_INC := $(VERISIMPLEPIR_DIR)/src/lib

# ============================================================================
# Configuration du compilateur
# ============================================================================
CPPSTD := -std=c++17

ifeq ($(SYSTEM),Darwin)
    # Configuration macOS
    CC := /Users/sofianeazogagh/local/llvm-19.1.7/bin/clang++ $(CPPSTD)
    LDFLAGS +=                -L/Users/sofianeazogagh/local/llvm-19.1.7/lib \
               -Wl,-rpath,/Users/sofianeazogagh/local/llvm-19.1.7/lib \
               -L/usr/local/lib \
               -L/opt/homebrew/opt/openssl@3/lib \
               -L/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/lib \
               -L$(VERISIMPLEPIR_DIR)/bin/lib \
               -Wl,-rpath,$(VERISIMPLEPIR_DIR)/bin/lib
    
    CPPFLAGS += -I/Users/sofianeazogagh/local/llvm-19.1.7/include \
                -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk \
                -I/opt/homebrew/opt/openssl@3/include \
                -I$(VERISIMPLEPIR_INC)
    
    LIBSUFFIX := .dylib
    EXESUFFIX :=
else
    # Configuration Linux
    CC := clang++ $(CPPSTD) -Wloop-analysis
    CPPFLAGS += -Wno-ignored-attributes
    LDFLAGS += -L/usr/local/lib \
               -L$(VERISIMPLEPIR_DIR)/bin/lib \
               -lrt
    LIBSUFFIX := .so
    EXESUFFIX :=
endif

# ============================================================================
# Options de compilation
# ============================================================================
CPPFLAGS += -O3 -Wall -fno-omit-frame-pointer

# SSE4 et AES uniquement sur x86_64
ifeq ($(UNAME_M),x86_64)
    CPPFLAGS += -maes -msse4
endif

# ============================================================================
# Bibliothèques
# ============================================================================
LDFLAGS += -lssl -lcrypto -lverisimplepir

# ============================================================================
# Structure du projet
# ============================================================================
SRCDIR := src
INCDIR := include
BUILDDIR := build
BINDIR := bin

# Sources de votre projet
SOURCES := $(wildcard $(SRCDIR)/*.cpp)
OBJECTS := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/%.o,$(SOURCES))
DEPENDS := $(OBJECTS:.o=.d)

# Nom de l'exécutable
TARGET := $(BINDIR)/pir_csv_client

# ============================================================================
# Règles principales
# ============================================================================
.PHONY: all clean directories

all: directories $(TARGET)

# Créer les répertoires nécessaires
directories:
	@mkdir -p $(BUILDDIR)
	@mkdir -p $(BINDIR)

# Lier l'exécutable
$(TARGET): $(OBJECTS) $(VERISIMPLEPIR_LIB)$(LIBSUFFIX)
	@echo "Linking $(TARGET)..."
	$(CC) -o $@ $(OBJECTS) $(LDFLAGS)
ifeq ($(SYSTEM),Darwin)
	@echo "Fixing library path..."
	@install_name_tool -change bin/lib/libverisimplepir.dylib $(VERISIMPLEPIR_DIR)/bin/lib/libverisimplepir.dylib $@ 2>/dev/null || true
endif
	@echo "Build complete: $(TARGET)"

# Compiler les fichiers source
$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp
	@echo "Compiling $<..."
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -I$(INCDIR) -MM -MT $@ $< > $(BUILDDIR)/$*.d
	$(CC) $(CPPFLAGS) -I$(INCDIR) -c -o $@ $<

# Vérifier que la bibliothèque VeriSimplePIR existe
$(VERISIMPLEPIR_LIB)$(LIBSUFFIX):
	@echo "Checking VeriSimplePIR library..."
	@if [ ! -f "$(VERISIMPLEPIR_LIB)$(LIBSUFFIX)" ]; then \
		echo "Error: VeriSimplePIR library not found at $(VERISIMPLEPIR_LIB)$(LIBSUFFIX)"; \
		echo "Please build VeriSimplePIR first by running 'make' in $(VERISIMPLEPIR_DIR)"; \
		exit 1; \
	fi

# Inclure les dépendances
-include $(DEPENDS)

# Nettoyage
clean:
	@echo "Cleaning..."
	$(RM) -rf $(BUILDDIR) $(BINDIR)
	@echo "Clean complete"

# Aide
help:
	@echo "Makefile pour projet PIR avec CSV"
	@echo ""
	@echo "Cibles disponibles:"
	@echo "  make          - Compiler le projet"
	@echo "  make clean    - Nettoyer les fichiers générés"
	@echo "  make help     - Afficher cette aide"
	@echo ""
	@echo "Configuration:"
	@echo "  VeriSimplePIR: $(VERISIMPLEPIR_DIR)"
	@echo "  Compilateur:   $(CC)"
	@echo "  Système:       $(SYSTEM) ($(UNAME_M))"