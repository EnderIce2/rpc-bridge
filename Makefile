.PHONY: all build clean

all:
	$(MAKE) -C src all

build:
	$(MAKE) -C src build

clean:
	$(MAKE) -C src clean
