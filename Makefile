CXX := g++
CXXFLAGS := -static -O3 -I. -isystem lib -isystem lib/lib -Llib/build -lpvzemu \
            -Wall -Wextra -Wcast-align -Wcast-qual -Wconversion -Wctor-dtor-privacy \
            -Wdisabled-optimization -Wformat=2 -Winit-self -Wlogical-op \
            -Wmissing-include-dirs -Wnoexcept -Wold-style-cast -Woverloaded-virtual \
            -Wredundant-decls -Wstrict-null-sentinel -Wswitch-default -Wswitch-enum \
            -Wsign-compare -Wsign-promo -Wundef -Wunused-variable -Wuseless-cast \
            -Wzero-as-null-pointer-constant -Wfatal-errors -Werror
MACOS_CXX ?= clang++
MACOS_ARCHS ?= x86_64;arm64
MACOS_BUILD_DIR := lib/build-macos-universal
MACOS_BIN_DIR := dest/bin/darwin
MACOS_CXXFLAGS := -arch x86_64 -arch arm64 -O3 -std=c++17 \
                  -I. -isystem lib -isystem lib/lib -L$(MACOS_BUILD_DIR) -lpvzemu \
                  -Wall -Wextra -Wfatal-errors
# Source files (excluding those starting with an underscore or debug)
SOURCES := $(filter-out _% debug%, $(wildcard *.cpp))
# Object files
OBJECTS := $(SOURCES:%.cpp=%)
# Targets
TARGETS := $(foreach obj, $(OBJECTS), dest/bin/$(obj))
MACOS_TARGETS := $(foreach obj, $(OBJECTS), $(MACOS_BIN_DIR)/$(obj))

# Default target: build the library and then compile sources
default: buildLib $(TARGETS)

# Build the library
buildLib:
	@echo "Building library..."
	@cd lib/build && cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebugInfo .. && ninja && cd ../../

# Build universal macOS binaries for VSIX packaging
macos: buildMacosLib $(MACOS_TARGETS)

buildMacosLib:
	@echo "Building universal macOS library..."
	@cmake -S lib -B $(MACOS_BUILD_DIR) -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebugInfo -DCMAKE_OSX_ARCHITECTURES="$(MACOS_ARCHS)"
	@cmake --build $(MACOS_BUILD_DIR)

# Compile sources
dest/bin/%: %.cpp
	@echo "Compiling $<..."
	@$(CXX) $< -o $@ $(CXXFLAGS)
	@echo.

$(MACOS_BIN_DIR)/%: %.cpp
	@echo "Compiling universal macOS $<..."
	@mkdir -p $(MACOS_BIN_DIR)
	@$(MACOS_CXX) $< -o $@ $(MACOS_CXXFLAGS)
	@chmod 755 $@
	@echo

# Clean the build
clean:
	@echo "Cleaning..."
	@rm -f $(TARGETS)
	@cd lib/build && ninja clean

.PHONY: default buildLib macos buildMacosLib clean
