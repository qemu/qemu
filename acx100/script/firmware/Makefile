.PHONY: extract_firmware

CC = gcc
CFLAGS = -Wall -O

extract: extract.o

extract_firmware: extract
	./extract acx100_pci.o > WLANGEN.BIN

clean:
	rm -f extract.o extract

