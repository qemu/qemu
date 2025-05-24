#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/cxl_switch0"
#define MAP_REGION_SIZE (4 * 1024) // 4KB mapping window

// Destructive string
char *trim_whitespace(char *str) {
  char *end;
  // Trim leading spaces
  while (isspace((unsigned char)*str))
    str++;
  // All spaces
  if (*str == 0) {
    return str;
  }
  // Trim trailing spaces
  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end))
    end--;
  // Write new null terminator character
  end[1] = '\0';
  return str;
}

void print_help() {
  printf("Interactive CXL Device Tester Commands:\n");
  printf("  r32 <hex_offset>            - Read a 32-bit value (dword) from "
         "hex_offset.\n");
  printf("  w32 <hex_offset> <hex_val>  - Write a 32-bit value (dword) to "
         "hex_offset.\n");
  printf("  r8  <hex_offset>            - Read an 8-bit value (byte) from "
         "hex_offset.\n");
  printf("  w8  <hex_offset> <hex_val>  - Write an 8-bit value (byte) to "
         "hex_offset.\n");
  printf("  help                        - Show this help message.\n");
  printf("  quit or q                   - Exit the tester.\n");
  printf(
      "Offsets and values are in hexadecimal (e.g., 0x100, FF, AABBCCDD).\n");
}

int main() {
  int fd;
  volatile uint8_t *mapped_base = NULL;
  char input_buffer[256];
  char command[32];
  unsigned long offset, value;

  printf("Interactive CXL Device Tester.\n");
  printf("Opening device: %s\n", DEVICE_PATH);

  fd = open(DEVICE_PATH, O_RDWR | O_SYNC);
  if (fd < 0) {
    perror("Failed to open device.");
    return EXIT_FAILURE;
  }
  printf("Device opened successfully (fd: %d).\n", fd);

  printf("Mapping device memory (size: %d bytes)...\n", MAP_REGION_SIZE);
  void *mmap_ptr =
      mmap(NULL, MAP_REGION_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mmap_ptr == MAP_FAILED) {
    perror("Failed to mmap device memory.");
    close(fd);
    return EXIT_FAILURE;
  }

  mapped_base = (volatile uint8_t *)mmap_ptr;
  printf("Device memory mmap'd successfully at address: %p\n", mmap_ptr);
  print_help();

  while (1) {
    printf("\ncxl_test> ");
    if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) {
      printf("EOF received, exiting\n");
      break;
    }

    char *trimmed_input = trim_whitespace(input_buffer);

    // Skip empty input
    if (strlen(trimmed_input) == 0) {
      continue;
    }

    int items_scanned =
        sscanf(trimmed_input, "%31s %lx %lx", command, &offset, &value);

    if (strcmp(command, "quit") == 0 || strcmp(command, "q") == 0) {
      printf("Exiting tester.\n");
      break;
    } else if (strcmp(command, "help") == 0 || strcmp(command, "h") == 0) {
      print_help();
    } else if ((strcmp(command, "r32") == 0) && items_scanned >= 2) {
      if (offset + sizeof(uint32_t) > MAP_REGION_SIZE) {
        printf(
            "Error: Offset 0x%lx (+%zu bytes) is out of mapped range (0x%X).\n",
            offset, sizeof(uint32_t), MAP_REGION_SIZE);
      } else {
        uint32_t read_val = *((volatile uint32_t *)(mapped_base + offset));
        printf("Read from 0x%04lx (32-bit): 0x%08X (%u)\n", offset, read_val,
               read_val);
      }
    } else if ((strcmp(command, "w32") == 0) && items_scanned >= 3) {
      if (offset + sizeof(uint32_t) > MAP_REGION_SIZE) {
        printf(
            "Error: Offset 0x%lx (+%zu bytes) is out of mapped range (0x%X).\n",
            offset, sizeof(uint32_t), MAP_REGION_SIZE);
      } else {
        *((volatile uint32_t *)(mapped_base + offset)) = (uint32_t)value;
        printf("Wrote 0x%08lX to 0x%04lx (32-bit).\n", value, offset);
      }
    } else if ((strcmp(command, "r8") == 0) && items_scanned >= 2) {
      if (offset + sizeof(uint8_t) > MAP_REGION_SIZE) {
        printf(
            "Error: Offset 0x%lx (+%zu byte) is out of mapped range (0x%X).\n",
            offset, sizeof(uint8_t), MAP_REGION_SIZE);
      } else {
        uint8_t read_val_8 = mapped_base[offset];
        printf("Read from 0x%04lx (8-bit):  0x%02X (%u)\n", offset, read_val_8,
               read_val_8);
      }
    } else if ((strcmp(command, "w8") == 0) && items_scanned >= 3) {
      if (offset + sizeof(uint8_t) > MAP_REGION_SIZE) {
        printf(
            "Error: Offset 0x%lx (+%zu byte) is out of mapped range (0x%X).\n",
            offset, sizeof(uint8_t), MAP_REGION_SIZE);
      } else {
        if (value > 0xFF) {
          printf("Warning: Value 0x%lX exceeds 8-bit range, will be truncated "
                 "to 0x%02X.\n",
                 value, (uint8_t)value);
        }
        mapped_base[offset] = (uint8_t)value;
        printf("Wrote 0x%02X to 0x%04lx (8-bit).\n", (uint8_t)value, offset);
      }
    } else {
      printf("Error: Unknown command or incorrect arguments. Type 'help' for "
             "commands.\n");
      printf("Scanned items: %d, Command: '%s', Offset: 0x%lx, Value: 0x%lx\n",
             items_scanned, command, offset, value);
    }
  }

  // Cleanup
  printf("Unmapping device memory...\n");
  if (mapped_base != NULL && munmap((void *)mapped_base, MAP_REGION_SIZE) < 0) {
    perror("Failed to unmap device memory.");
  } else {
    printf("Device memory unmapped successfully.\n");
  }

  if (fd >= 0) {
    close(fd);
    printf("Device file descriptor closed.\n");
  }

  return EXIT_SUCCESS;
}