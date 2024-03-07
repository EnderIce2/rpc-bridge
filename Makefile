C_SOURCES = $(shell find ./ -type f -name '*.c')
C_OBJECTS = $(C_SOURCES:.c=.o)

CFLAGS = -std=c17 -Wno-int-conversion
LFLAGS =

# DBGFLAGS = -Wl,--export-all-symbols -g -O0 -ggdb3 -Wall

all: build

# This is valid only if this directory is a subdirectory of drive_c
install: build
	cp build/bridge.exe ../bridge.exe

build: $(C_OBJECTS)
	$(info Linking)
	x86_64-w64-mingw32-gcc $(C_OBJECTS) $(LFLAGS) $(DBGFLAGS) -o build/bridge.exe

%.o: %.c
	$(info Compiling $<)
	x86_64-w64-mingw32-gcc $(CFLAGS) $(DBGFLAGS) -c $< -o $@

clean:
	rm -f $(C_OBJECTS) build/bridge.exe
