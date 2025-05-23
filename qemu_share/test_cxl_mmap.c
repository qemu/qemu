#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define DEVICE_PATH "/dev/cxl_switch0"
#define MAP_SIZE (256 * 1024 * 1024) // Should match replicated_mem_size

int main() {
  int fd;
  volatile uint32_t *mapped_mem; // Use volatile for device memory

  fd = open(DEVICE_PATH, O_RDWR | O_SYNC);
  if (fd < 0) {
    perror("Failed to open device");
    return 1;
  }

  // Map the entire BAR0 region
  mapped_mem = mmap(NULL, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapped_mem == MAP_FAILED) {
    perror("Failed to mmap device");
    close(fd);
    return 1;
  }

  printf("Device mmap'd successfully. Pointer: %p\n", mapped_mem);

  // Example: Write a value, then read it back
  printf("Initial value at offset 0: 0x%08x\n", mapped_mem[0]);
  mapped_mem[0] = 0xAABBCCDD;
  printf("Wrote 0xAABBCCDD to offset 0.\n");
  printf("Value read back from offset 0: 0x%08x\n", mapped_mem[0]);

  // Example: Write to a different offset
  // Ensure this offset is within MAP_SIZE
  size_t offset_dw = 1024; // 1024 * 4 bytes = 4KB offset
  if ((offset_dw * sizeof(uint32_t)) < MAP_SIZE) {
    mapped_mem[offset_dw] = 0x12345678;
    printf("Wrote 0x12345678 to dword offset %zu.\n", offset_dw);
    printf("Value read back from dword offset %zu: 0x%08x\n", offset_dw,
           mapped_mem[offset_dw]);
  }

  // Check the host replica files after these writes
  printf("Check the host replica files now for the written values.\n");
  printf("Press Enter to unmap and exit...\n");
  getchar();

  if (munmap((void *)mapped_mem, MAP_SIZE) == -1) {
    perror("Failed to munmap");
  }
  close(fd);
  return 0;
}