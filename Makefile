.PHONY: all
all: build/zip

build/zip: zip.c zip.h
	$(CC) $< -o $@