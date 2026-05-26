CC ?= cc
CFLAGS += -msse4.2 -I/ucrt64/include/
LDLIBS += -L/ucrt64/lib/ -lz

.PHONY: all
all: build/zip

build/zip: zip.c zip.h
	mkdir -p build
	$(CC) $(CFLAGS) zip.c -o $@ $(LDLIBS)