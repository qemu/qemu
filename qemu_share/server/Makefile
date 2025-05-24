# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -g -std=c11
LDFLAGS = -pthread

# Source files
SRCS = cxl_switch_server.c cxl_server_admin.c

# Object files are derived from source files (e.g., cxl_switch_server.o cxl_server_admin.o)
OBJS = $(SRCS:.c=.o)

# Header files that object files depend on
HDRS = cxl_switch_ipc.h

# Target executable names, derived from source files
TARGETS = $(SRCS:.c=) # This will result in "cxl_switch_server cxl_server_admin"

# Default target: build all specified executables
all: $(TARGETS)

# --- Rules to build specific executables from their object files ---

# Rule to link the cxl_switch_server executable
# Depends on its corresponding object file: cxl_switch_server.o
cxl_switch_server: cxl_switch_server.o
	@echo "Linking $@ from $^"
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Rule to link the cxl_server_admin executable
# Depends on its corresponding object file: cxl_server_admin.o
cxl_server_admin: cxl_server_admin.o
	@echo "Linking $@ from $^"
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# --- Generic rule to compile .c source files into .o object files ---

# This pattern rule applies to any .o file that needs to be built from a .c file.
# It crucially includes $(HDRS) in the prerequisites.
# If any header in $(HDRS) changes, or the .c file itself changes,
# the object file will be recompiled.
# $< refers to the first prerequisite (the .c file).
# $@ refers to the target file (the .o file).
%.o: %.c $(HDRS)
	@echo "Compiling $< to $@"
	$(CC) $(CFLAGS) -c $< -o $@

# Rule to clean up build artifacts (object files and executables)
clean:
	@echo "Cleaning up build artifacts..."
	rm -f $(OBJS) $(TARGETS)

# Phony targets (targets that are not actual files)
# 'all' and 'clean' are common phony targets.
.PHONY: all clean
