CC = gcc
CFLAGS = -Wall -Wextra -g -O2 -std=c11

SRCS = sequential_writer.c sequential_reader.c interactive_tester.c

TARGETS = $(SRCS:.c=)

# Default target: build all executables
all: $(TARGETS)

# Rule to build each executable from its .c file
# $< is an automatic variable for the first prerequisite (the .c file)
# $@ is an automatic variable for the target name (the executable)
sequential_writer: sequential_writer.c
	$(CC) $(CFLAGS) -o $@ $<

sequential_reader: sequential_reader.c
	$(CC) $(CFLAGS) -o $@ $<

interactive_tester: interactive_tester.c
	$(CC) $(CFLAGS) -o $@ $<

# Phony target for cleaning up
# .PHONY ensures that 'clean' will run even if a file named 'clean' exists
.PHONY: clean
clean:
	rm -f $(TARGETS)
