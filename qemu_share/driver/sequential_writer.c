#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <errno.h>

#define DEVICE_PATH "/dev/cxl_switch0"
#define MAP_REGION_SIZE (4096)

#define TURN_FLAG_OFFSET 0UL
#define NUMBER_OFFSET (TURN_FLAG_OFFSET + sizeof(uint32_t))

#define WRITER_CAN_WRITE 0
#define READER_CAN_READ  1
#define MAX_NUMBER 100

int main() {
    int fd;
    volatile uint8_t *mapped_base;
    volatile uint32_t *turn_flag_ptr;
    volatile uint32_t *number_ptr;
    uint32_t current_num_to_write = 1;

    printf("Sequential Writer starting (will write numbers 1 to %d).\n", MAX_NUMBER);

    fd = open(DEVICE_PATH, O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("Writer: Failed to open device");
        return EXIT_FAILURE;
    }
    printf("Writer: Device %s opened successfully (fd: %d).\n", DEVICE_PATH, fd);

    void *mmap_ptr = mmap(
        NULL,
        MAP_REGION_SIZE,
        PROT_READ | PROT_WRITE,
        MAP_SHARED,
        fd,
        0
    );

    if (mmap_ptr == MAP_FAILED) {
        perror("Writer: Failed to mmap device");
        close(fd);
        return EXIT_FAILURE;
    }
    mapped_base = (volatile uint8_t *)mmap_ptr;
    turn_flag_ptr = (volatile uint32_t *)(mapped_base + TURN_FLAG_OFFSET);
    number_ptr = (volatile uint32_t *)(mapped_base + NUMBER_OFFSET);

    printf("Writer: Device memory mmap'd successfully at %p.\n", mmap_ptr);
    printf("         Turn flag at: %p\n", (void*)turn_flag_ptr);
    printf("         Number data at: %p\n", (void*)number_ptr);

    // Initialize: Writer goes first
    *turn_flag_ptr = WRITER_CAN_WRITE;
    *number_ptr = 0; // Initial dummy value
    printf("Writer: Initialized turn flag to WRITER_CAN_WRITE (%u).\n", WRITER_CAN_WRITE);

    while (current_num_to_write <= MAX_NUMBER) {
        // Wait for our turn
        while (*turn_flag_ptr != WRITER_CAN_WRITE) {
            // printf("Writer: Waiting for its turn (flag is %u)...\r", *turn_flag_ptr);
            // fflush(stdout);
            usleep(100000); // Poll every 100ms
        }

        // It's our turn, write the number
        *number_ptr = current_num_to_write;
        printf("Writer: Wrote number %u.\n", *number_ptr);

        // Signal reader it's their turn
        *turn_flag_ptr = READER_CAN_READ;
        printf("Writer: Set turn flag to READER_CAN_READ (%u).\n", READER_CAN_READ);

        current_num_to_write++;

        if (current_num_to_write <= MAX_NUMBER) {
            printf("Writer: Sleeping for 1 second...\n\n");
            sleep(1);
        } else {
            printf("Writer: Finished writing all numbers.\n");
        }
    }

    printf("Writer: All numbers written. Exiting.\n");

    // Cleanup
    if (mmap_ptr != MAP_FAILED) {
        munmap(mmap_ptr, MAP_REGION_SIZE);
    }
    if (fd >= 0) {
        close(fd);
    }

    return EXIT_SUCCESS;
}
