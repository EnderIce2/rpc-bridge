C_SOURCES = $(shell find ./ -type f -name '*.c')
C_OBJECTS = $(C_SOURCES:.c=.o)

GIT_COMMIT = $(shell git rev-parse --short HEAD)
GIT_BRANCH = $(shell git rev-parse --abbrev-ref HEAD)

CWARNFLAGS = -Wno-int-conversion -Wno-incompatible-pointer-types

CFLAGS = -std=c17 -DGIT_COMMIT='"$(GIT_COMMIT)"' -DGIT_BRANCH='"$(GIT_BRANCH)"'
LFLAGS = -lgdi32 -lws2_32

# DBGFLAGS = -Wl,--export-all-symbols -g -O0 -ggdb3 -Wall

all: build

build: $(C_OBJECTS)
	$(info Linking)
	x86_64-w64-mingw32-windres bridge.rc -O coff -o bridge.res
	x86_64-w64-mingw32-gcc $(C_OBJECTS) bridge.res $(LFLAGS) $(DBGFLAGS) -o build/bridge.exe

%.o: %.c
	$(info Compiling $<)
	x86_64-w64-mingw32-gcc $(CFLAGS) $(CWARNFLAGS) $(DBGFLAGS) -c $< -o $@

clean:
	rm -f $(C_OBJECTS) build/bridge.exe bridge.res
