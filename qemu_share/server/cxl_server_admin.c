#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "cxl_switch_ipc.h"

void print_usage(const char *prog_name) {
    fprintf(stderr, "Usage: %s fail <replica_index>\n", prog_name);
    // TODO: Expand this to include more commands
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *admin_socket_path = CXL_SWITCH_SERVER_ADMIN_SOCKET_PATH_DEFAULT;
    int sockfd;
    struct sockaddr_un addr;

    cxl_admin_command_t admin_cmd;
    cxl_admin_response_t admin_resp;

    // Parse command
    if (strcmp(argv[1], "fail") == 0) {
        admin_cmd.cmd_type = CXL_ADMIN_CMD_TYPE_FAIL_REPLICA;
        admin_cmd.replica_index = (uint8_t) atoi(argv[2]);
    } else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    // Create socket
    sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Failed to create socket");
        return EXIT_FAILURE;
    }

    // Prepare address structure
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, admin_socket_path, sizeof(addr.sun_path) - 1);

    // Connect to the admin socket
    printf("Connecting to admin socket at %s...\n", addr.sun_path);
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Failed to connect to admin socket");
        close(sockfd);
        return EXIT_FAILURE;
    }
    printf("Connected to admin socket successfully.\n");

    // Send admin command
    printf("Sending admin command type %u for replica %u...\n",
           admin_cmd.cmd_type, admin_cmd.replica_index);
    if (send(sockfd, &admin_cmd, sizeof(admin_cmd), 0) != sizeof(admin_cmd)) {
        perror("Failed to send admin command");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // Recv admin response
    ssize_t n = recv(sockfd, &admin_resp, sizeof(admin_resp), MSG_WAITALL);
    if (n <= 0) {
        if (n < 0) {
            perror("Failed to receive admin response");
        } else {
            fprintf(stderr, "Admin client disconnected\n");
        }
        close(sockfd);
        return EXIT_FAILURE;
    }
    if (n != sizeof(admin_resp)) {
        fprintf(stderr, "Received invalid admin response size: expected %zu, got %zd\n",
                sizeof(admin_resp), n);
        close(sockfd);
        return EXIT_FAILURE;
    }
    printf("Received admin response status: %u\n", admin_resp.status);
    switch (admin_resp.status) {
        case CXL_IPC_STATUS_OK:
            printf("Admin command executed successfully.\n");
            break;
        case CXL_IPC_STATUS_ERROR_INVALID_REQ:
            fprintf(stderr, "Error: Invalid admin request.\n");
            break;
        case CXL_IPC_STATUS_ERROR_GENERIC:
            fprintf(stderr, "Error: Generic error occurred.\n");
            break;
        default:
            fprintf(stderr, "Error: Unknown status %u received.\n", admin_resp.status);
            break;
    }

    close(sockfd);
    return (admin_resp.status == CXL_IPC_STATUS_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}