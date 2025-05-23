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
    uint32_t last_read_number = 0;

    printf("Sequential Reader starting (will read numbers up to %d).\n", MAX_NUMBER);

    fd = open(DEVICE_PATH, O_RDWR | O_SYNC); // Need RDWR to change the turn flag
    if (fd < 0) {
        perror("Reader: Failed to open device");
        return EXIT_FAILURE;
    }
    printf("Reader: Device %s opened successfully (fd: %d).\n", DEVICE_PATH, fd);

    void *mmap_ptr = mmap(
        NULL,
        MAP_REGION_SIZE,
        PROT_READ | PROT_WRITE, // Need PROT_WRITE to change the turn flag
        MAP_SHARED,
        fd,
        0
    );

    if (mmap_ptr == MAP_FAILED) {
        perror("Reader: Failed to mmap device");
        close(fd);
        return EXIT_FAILURE;
    }
    mapped_base = (volatile uint8_t *)mmap_ptr;
    turn_flag_ptr = (volatile uint32_t *)(mapped_base + TURN_FLAG_OFFSET);
    number_ptr = (volatile uint32_t *)(mapped_base + NUMBER_OFFSET);

    printf("Reader: Device memory mmap'd successfully at %p.\n", mmap_ptr);
    printf("         Turn flag at: %p\n", (void*)turn_flag_ptr);
    printf("         Number data at: %p\n", (void*)number_ptr);
    printf("Reader: Polling for numbers...\n");

    while (last_read_number < MAX_NUMBER) {
        // Wait for our turn
        while (*turn_flag_ptr != READER_CAN_READ) {
            // printf("Reader: Waiting for its turn (flag is %u)...\r", *turn_flag_ptr);
            // fflush(stdout);
            usleep(100000); // Poll every 100ms
        }

        // It's our turn, read the number
        last_read_number = *number_ptr;
        printf("Reader: Read number %u.\n", last_read_number);

        // Signal writer it's their turn
        *turn_flag_ptr = WRITER_CAN_WRITE;
        printf("Reader: Set turn flag to WRITER_CAN_WRITE (%u).\n", WRITER_CAN_WRITE);

        if (last_read_number < MAX_NUMBER) {
            printf("Reader: Sleeping for 1 second...\n\n");
            sleep(1);
        } else {
            printf("Reader: Read the last number (%u).\n", MAX_NUMBER);
        }
    }

    printf("Reader: All numbers read. Exiting.\n");

    // Cleanup
    if (mmap_ptr != MAP_FAILED) {
        munmap(mmap_ptr, MAP_REGION_SIZE);
    }
    if (fd >= 0) {
        close(fd);
    }

    return EXIT_SUCCESS;
}
