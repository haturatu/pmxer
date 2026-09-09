SHELL := /bin/bash

.DEFAULT_GOAL := build

PRESET ?= linux-dev
BUILD_DIR := build/$(PRESET)
BINARY := $(BUILD_DIR)/pmxer

JOBS ?=
ARGS ?=
CMAKE_ARGS ?=
PREFIX ?= $(CURDIR)/install

LIBMMD_DIR := external/libmmd

.PHONY: \
	help setup submodules \
	configure build cli debug sanitize release \
	test run ci compdb \
	install clean distclean presets

# ------------------------------------------------------------
# Setup
# ------------------------------------------------------------

setup: submodules

submodules:
	@git submodule update --init --recursive -- "$(LIBMMD_DIR)"
	@test -f "$(LIBMMD_DIR)/CMakeLists.txt" || { \
		printf '%s\n' "error: failed to initialize libmmd submodule" >&2; \
		exit 1; \
	}

# ------------------------------------------------------------
# Build
# ------------------------------------------------------------

configure: submodules
	cmake --preset "$(PRESET)" $(CMAKE_ARGS)

build: configure
	cmake --build --preset "$(PRESET)" \
		$(if $(JOBS),--parallel $(JOBS),--parallel)

debug:
	@$(MAKE) PRESET=linux-dev build

cli:
	@$(MAKE) PRESET=linux-cli build

sanitize:
	@$(MAKE) PRESET=linux-clang build

release:
	@$(MAKE) PRESET=linux-release build

# ------------------------------------------------------------
# Test / run
# ------------------------------------------------------------

test: build
	ctest --preset "$(PRESET)" --output-on-failure

run: build
	"./$(BINARY)" $(ARGS)

ci: submodules
	cmake --preset linux-cli \
		-DPMXER_WARNINGS_AS_ERRORS=ON \
		$(CMAKE_ARGS)
	cmake --build --preset linux-cli \
		$(if $(JOBS),--parallel $(JOBS),--parallel)
	ctest --preset linux-cli --output-on-failure

# ------------------------------------------------------------
# Development
# ------------------------------------------------------------

compdb: configure
	ln -sfn "$(BUILD_DIR)/compile_commands.json" compile_commands.json

# ------------------------------------------------------------
# Install / cleanup
# ------------------------------------------------------------

install: build
	cmake --install "$(BUILD_DIR)" --prefix "$(PREFIX)"

clean:
	@if [[ -d "$(BUILD_DIR)" ]]; then \
		cmake --build --preset "$(PRESET)" --target clean; \
	fi

distclean:
	rm -rf build install compile_commands.json

presets:
	cmake --list-presets=all

# ------------------------------------------------------------
# Help
# ------------------------------------------------------------

help:
	@printf '%s\n' \
		'Usage: make [target] [VARIABLE=value]' \
		'' \
		'Build:' \
		'  build       Build linux-dev by default' \
		'  debug       Build GUI debug version' \
		'  cli         Build CLI-only version' \
		'  sanitize    Build Clang + ASan/UBSan version' \
		'  release     Build Linux release version' \
		'' \
		'Test / run:' \
		'  test        Build and run tests' \
		'  run         Build and run pmxer' \
		'  ci          Run local Linux CI checks' \
		'' \
		'Development:' \
		'  compdb      Link compile_commands.json for clangd' \
		'' \
		'Other:' \
		'  install     Install into PREFIX (default: ./install)' \
		'  clean       Clean current preset' \
		'  distclean   Remove generated files' \
		'  presets     List CMake presets' \
		'  setup       Initialize the libmmd submodule' \
		'  help        Show this help' \
		'' \
		'Examples:' \
		'  make' \
		'  make cli' \
		'  make test' \
		'  make run ARGS="model.pmx"' \
		'  make run ARGS="info model.pmx"' \
		'  make PRESET=linux-cli test' \
		'  make release JOBS=16'
