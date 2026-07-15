CXX := clang++
ROOT := ../..
TARGETS := spawn_test.exe finish_table_test.exe
LIBPVZEMU := $(ROOT)/lib/build/libpvzemu.a
REFRESH_SPAWN_HEADERS := $(ROOT)/seml/refresh/spawn.h $(ROOT)/seml/refresh/types.h

CXXFLAGS := -std=c++17 -static -O3 -I$(ROOT) -isystem $(ROOT)/lib -isystem $(ROOT)/lib/lib \
            -Wall -Wextra -Wpedantic
LDFLAGS := -L$(ROOT)/lib/build -lpvzemu -lshell32

.PHONY: all build-lib clean

all: build-lib
	@$(MAKE) $(TARGETS)

build-lib:
	@cmake -S $(ROOT)/lib -B $(ROOT)/lib/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebugInfo
	@cmake --build $(ROOT)/lib/build

spawn_test.exe: main.cpp spawn_test.cpp spawn_test.h $(REFRESH_SPAWN_HEADERS) $(LIBPVZEMU)
	@$(CXX) main.cpp spawn_test.cpp -o $@ $(CXXFLAGS) $(LDFLAGS)

finish_table_test.exe: finish_table_test.cpp spawn_test.cpp spawn_test.h \
                       $(REFRESH_SPAWN_HEADERS) $(LIBPVZEMU)
	@$(CXX) finish_table_test.cpp spawn_test.cpp -o $@ $(CXXFLAGS) $(LDFLAGS)

clean:
	@for %%f in ($(TARGETS)) do @if exist "%%f" del /Q "%%f"
