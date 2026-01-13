# Makefile pour projet utilisant VeriSimplePIR avec CSV
# Multi OS makefile (macOS et Linux)

UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
PKG_CONFIG ?= pkg-config

# ============================================================================
# Chemins vers VeriSimplePIR (relatifs au projet)
# ============================================================================
VERISIMPLEPIR_DIR := $(shell pwd)/VeriSimplePIR
VERISIMPLEPIR_LIB := $(VERISIMPLEPIR_DIR)/bin/lib/libverisimplepir
VERISIMPLEPIR_INC := $(VERISIMPLEPIR_DIR)/src/lib

# ============================================================================
# Configuration du compilateur (portable)
# ============================================================================
CC ?= clang++
CPPFLAGS += -std=c++17 -O3 -Wall -fno-omit-frame-pointer

# SSE4 / AES seulement sur x86_64
ifeq ($(UNAME_M),x86_64)
    CPPFLAGS += -maes -msse4
endif

# ============================================================================
# OpenSSL via pkg-config (fallback sur -lssl -lcrypto)
# ============================================================================
OPENSSL_CFLAGS := $(shell $(PKG_CONFIG) --cflags openssl 2>/dev/null)
OPENSSL_LIBS   := $(shell $(PKG_CONFIG) --libs openssl 2>/dev/null)
CPPFLAGS += $(OPENSSL_CFLAGS) -I$(VERISIMPLEPIR_INC)
LDFLAGS  += $(if $(OPENSSL_LIBS),$(OPENSSL_LIBS),-lssl -lcrypto)

# ============================================================================
# Support Parquet (optionnel, activé si PARQUET_SUPPORT=1 et pkg-config ok)
# ============================================================================
PARQUET_SUPPORT ?= 0
ifeq ($(PARQUET_SUPPORT),1)
    ARROW_CFLAGS  := $(shell $(PKG_CONFIG) --cflags arrow parquet 2>/dev/null)
    ARROW_LIBS    := $(shell $(PKG_CONFIG) --libs arrow parquet 2>/dev/null)
    ifneq ($(ARROW_CFLAGS)$(ARROW_LIBS),)
        CPPFLAGS += -DPARQUET_SUPPORT $(ARROW_CFLAGS)
        LDFLAGS  += $(ARROW_LIBS)
    else
        $(warning PARQUET_SUPPORT=1 mais Arrow/Parquet introuvables via pkg-config)
    endif
endif

# ============================================================================
# Bibliothèque VeriSimplePIR
# ============================================================================
LDFLAGS += -L$(VERISIMPLEPIR_DIR)/bin/lib -Wl,-rpath,$(VERISIMPLEPIR_DIR)/bin/lib -lverisimplepir
ifeq ($(UNAME_S),Darwin)
    LDFLAGS += -lc++
endif
LIBSUFFIX := $(if $(filter $(UNAME_S),Darwin),.dylib,.so)
EXESUFFIX :=

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
ifeq ($(UNAME_S),Darwin)
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
	@echo "  Système:       $(UNAME_S) ($(UNAME_M))"