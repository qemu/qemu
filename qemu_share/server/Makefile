# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -std=c11
LDFLAGS = -pthread

# Source and object files
SRCS = cxl_switch_server.c
OBJS = $(SRCS:.c=.o)
HDRS = cxl_switch_ipc.h

# Target executable name
TARGET = cxl_switch_server

# Default target
all: $(TARGET)

# Rule to link the executable
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -o $(TARGET) $(LDFLAGS)

# Rule to compile source files into object files
# The .c.o suffix rule will handle this, but being explicit for clarity:
%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to clean up build artifacts
clean:
	rm -f $(OBJS) $(TARGET)

# Phony targets (targets that are not files)
.PHONY: all clean