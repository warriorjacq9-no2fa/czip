CC ?= cc
CFLAGS += -msse4.2 -O2 -g -Wall
LDFLAGS +=
LDLIBS += -lz

.PHONY: all
all: build/zip

build/zip: zip.c zip.h
	mkdir -p build
	$(CC) $(CFLAGS) zip.c -o $@ $(LDFLAGS) $(LDLIBS)