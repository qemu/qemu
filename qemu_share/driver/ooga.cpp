#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <ostream>
#include <sys/mman.h>
#include <sys/types.h>
#include <system_error>
#include <unistd.h>

#define DEVICE_PATH "/dev/cxl_switch_client0"

#define MMAP_PGOFF_BAR0 0
#define MMAP_PGOFF_BAR1 1
#define MMAP_PGOFF_BAR2 2

#define BAR0_MAILBOX_SIZE 0x1000         // 4KB
#define BAR1_CONTROL_SIZE 0x1000         // 4KB
#define BAR2_DATA_SIZE (1 * 1024 * 1024) // 1MB

#define REG_COMMAND_DOORBELL 0x00
#define REG_COMMAND_STATUS 0x04

#define CMD_STATUS_IDLE 0x00
#define CMD_STATUS_PROCESSING 0x01
#define CMD_STATUS_RESPONSE_READY 0x02
#define CMD_STATUS_ERROR_IPC 0xE0
#define CMD_STATUS_ERROR_SERVER 0xE1
#define CMD_STATUS_ERROR_INTERNAL 0xE2
#define CMD_STATUS_ERROR_BUSY 0xE3
#define CMD_STATUS_ERROR_BAD_WINDOW_CONFIG 0xE4

void *map_bar(int fd, off_t page_offset, size_t bar_size,
              const std::string &bar_name) {
  long pagesize = getpagesize();

  off_t mmap_offset = page_offset * pagesize;

  std::cout << "Mapping " << bar_name << " (size: " << bar_size
            << " bytes) at offset " << page_offset << " (byte offset "
            << mmap_offset << ")" << std::endl;

  void *mapped_ptr =
      mmap(NULL, bar_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, mmap_offset);

  if (mapped_ptr == MAP_FAILED) {
    throw std::system_error(errno, std::generic_category(),
                            "Failed to mmap " + bar_name);
  }
  std::cout << bar_name << " mapped successfully at address: " << mapped_ptr
            << std::endl;
  return mapped_ptr;
}

int main() {
  int fd = -1;
  volatile uint8_t *bar0_ptr = nullptr;
  volatile uint8_t *bar1_ptr = nullptr;
  volatile uint8_t *bar2_ptr = nullptr;

  try {
    std::cout << "Opening device: " << DEVICE_PATH << std::endl;
    fd = open(DEVICE_PATH, O_RDWR | O_SYNC);
    if (fd < 0) {
      throw std::system_error(
          errno, std::generic_category(), "Failed to open device");
    }

    std::cout << "Device opened successfully (fd: " << fd << ")." << std::endl;

    bar0_ptr = static_cast<volatile uint8_t *>(
        map_bar(fd, MMAP_PGOFF_BAR0, BAR0_MAILBOX_SIZE, "BAR0 Mailbox"));
    
    bar1_ptr = static_cast<volatile uint8_t *>(
        map_bar(fd, MMAP_PGOFF_BAR1, BAR1_CONTROL_SIZE, "BAR1 Control"));

    bar2_ptr = static_cast<volatile uint8_t *>(
        map_bar(fd, MMAP_PGOFF_BAR2, BAR2_DATA_SIZE, "BAR2 Data Window"));
      
    std::cout << "All BARs mapped successfully." << std::endl;

    // Test reading and writing to BARs
    if (bar1_ptr) {
      volatile uint32_t* status_reg_ptr = reinterpret_cast<volatile uint32_t*>(bar1_ptr + REG_COMMAND_STATUS);
      uint32_t initial_status = *status_reg_ptr;
      std::cout << "Initial Command Status: 0x" << std::hex << initial_status
                << std::dec << std::endl;
      if (initial_status == CMD_STATUS_IDLE) {
        std::cout << "Command Status is IDLE, expected." << std::endl;
      } else {
        std::cout << "Command Status is not IDLE, current status: 0x, might have done an oof"
                  << std::hex << initial_status << std::dec << std::endl;
      }

      if (bar0_ptr) {
        uint32_t test_offset_in_mailbox = 0x10;
        uint32_t test_value_mailbox = 0xCAFEFACE;
        volatile uint32_t* mailbox_loc = reinterpret_cast<volatile uint32_t*>(bar0_ptr + test_offset_in_mailbox);

        std::cout << "Writing 0x" << std::hex << test_value_mailbox
                  << " to BAR0 Mailbox at offset 0x" << test_offset_in_mailbox
                  << std::dec << std::endl;

        *mailbox_loc = test_value_mailbox;

        std::cout << "Reading back from BAR0 Mailbox at offset 0x"
                  << test_offset_in_mailbox << ": 0x" << std::hex
                  << *mailbox_loc << std::dec << std::endl;
        uint32_t read_value_mailbox = *mailbox_loc;
        if (read_value_mailbox == test_value_mailbox) {
          std::cout << "BAR0 Mailbox write/read test passed." << std::endl;
        } else {
          std::cout << "BAR0 Mailbox write/read test failed. Expected 0x"
                    << std::hex << test_value_mailbox << ", got 0x"
                    << read_value_mailbox << std::dec << std::endl;
        }
      }
    }
  } catch (const std::system_error &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    if (fd >= 0) close(fd);
    return EXIT_FAILURE;
  } catch (const std::exception) {
    std::cerr << "An unexpected error occurred." << std::endl;
    if (fd >= 0) close(fd);
    return EXIT_FAILURE;
  }

  std::cout << "Cleaning up" << std::endl;
  if (bar0_ptr && munmap(const_cast<uint8_t*>(bar0_ptr), BAR0_MAILBOX_SIZE) == -1) {
    perror("Failed to munmap BAR0");
  }
  if (bar1_ptr && munmap(const_cast<uint8_t*>(bar1_ptr), BAR1_CONTROL_SIZE) == -1) {
    perror("Failed to munmap BAR1");
  }
  if (bar2_ptr && munmap(const_cast<uint8_t*>(bar2_ptr), BAR2_DATA_SIZE) == -1) {
    perror("Failed to munmap BAR2");
  }
  if (fd >= 0 && close(fd) == -1) {
    perror("Failed to close device file");
  }
  std::cout << "Exit" << std::endl;
  return EXIT_SUCCESS;
}