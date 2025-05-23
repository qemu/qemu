#include <bits/pthreadtypes.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "cxl_switch_ipc.h"

#define CXL_SWITCH_SERVER_DEBUG 1
#define CXL_SWITCH_SERVER_DEBUG_PRINT(...)                                     \
  do {                                                                         \
    if (CXL_SWITCH_SERVER_DEBUG) {                                             \
      fprintf(stderr, __VA_ARGS__);                                            \
    }                                                                          \
  } while (0)

#define MAX_CLIENTS 5
#define NUM_REPLICAS 3

// Server configuration
typedef struct {
  char *socket_path;
  char *replica_paths[NUM_REPLICAS];
  uint64_t replicated_mem_size;
} server_config_t;

// Server's global state, protected by mutex
typedef struct {
  server_config_t config;
  int replica_fds[NUM_REPLICAS];
  uint8_t *replica_mmap_addrs[NUM_REPLICAS];
  cxl_ipc_status_t replica_status[NUM_REPLICAS];
  pthread_mutex_t mutex;
} server_state_t;

server_state_t server_state;



/** Add function prototypes so clang doesnt complain */
int init_replicas(server_state_t *state);
void cleanup_replicas(server_state_t *state);
void handle_get_mem_size(int client_fd, server_state_t *state);
void handle_write_req(int client_fd, const cxl_ipc_write_req_t *req,
                      server_state_t *state);
void handle_read_req(int client_fd, const cxl_ipc_read_req_t *req,
                     server_state_t *state);
void handle_client_request(int client_fd, server_state_t *state);

int init_replicas(server_state_t *state) {
  for (int i = 0; i < NUM_REPLICAS; i++) {
    state->replica_fds[i] = open(state->config.replica_paths[i], O_RDWR);
    if (state->replica_fds[i] < 0) {
      perror("Failed to open replica file");
      CXL_SWITCH_SERVER_DEBUG_PRINT("Failed to open replica file: %s\n",
                                    state->config.replica_paths[i]);
      // Close already opened files
      for (int j = 0; j < i; j++) {
        close(state->replica_fds[j]);
      }
      return -1;
    }

    struct stat sb;
    if (fstat(state->replica_fds[i], &sb) == -1) {
      perror("Server: fstat error");
      close(state->replica_fds[i]);
      // Close already opened files
      for (int j = 0; j < i; j++) {
        close(state->replica_fds[j]);
      }
      return -1;
    }
    if ((uint64_t)sb.st_size < state->config.replicated_mem_size) {
      CXL_SWITCH_SERVER_DEBUG_PRINT(
          "Replica file %s is smaller than expected size %" PRIu64 " bytes\n",
          state->config.replica_paths[i], state->config.replicated_mem_size);
      close(state->replica_fds[i]);
      // Close already opened files
      for (int j = 0; j < i; j++) {
        close(state->replica_fds[j]);
      }
      return -1;
    }

    state->replica_mmap_addrs[i] =
        mmap(NULL, state->config.replicated_mem_size, PROT_READ | PROT_WRITE,
             MAP_SHARED, state->replica_fds[i], 0);
    if (state->replica_mmap_addrs[i] == MAP_FAILED) {
      perror("Server: Failed to mmap replica file");
      CXL_SWITCH_SERVER_DEBUG_PRINT(
          "Failed to mmap replica file %s, error: %s\n",
          state->config.replica_paths[i], strerror(errno));
      close(state->replica_fds[i]);

      // Unmap and close previous files
      for (int j = 0; j < i; j++) {
        munmap(state->replica_mmap_addrs[j], state->config.replicated_mem_size);
        close(state->replica_fds[j]);
      }
      return -1;
    }
    state->replica_status[i] = CXL_IPC_STATUS_OK;
    CXL_SWITCH_SERVER_DEBUG_PRINT(
        "Replica file %s mapped successfully at address %p\n",
        state->config.replica_paths[i], state->replica_mmap_addrs[i]);
  }
  return 0;
}

void cleanup_replicas(server_state_t *state) {
  for (int i = 0; i < NUM_REPLICAS; i++) {
    if (state->replica_mmap_addrs[i] != MAP_FAILED &&
        state->replica_mmap_addrs[i] != NULL) {
      munmap(state->replica_mmap_addrs[i], state->config.replicated_mem_size);
      state->replica_mmap_addrs[i] = NULL;
    }
    if (state->replica_fds[i] >= 0) {
      close(state->replica_fds[i]);
      state->replica_fds[i] = -1;
    }
  }
}

void handle_get_mem_size(int client_fd, server_state_t *state) {
  cxl_ipc_get_mem_size_resp_t resp;
  resp.type = CXL_MSG_TYPE_GET_MEM_SIZE_RESP;
  resp.status = CXL_IPC_STATUS_OK;
  resp.mem_size = state->config.replicated_mem_size;

  CXL_SWITCH_SERVER_DEBUG_PRINT(
      "Server: sending memory size response to client, size: %" PRIu64
      " bytes\n",
      resp.mem_size);

  if (send(client_fd, &resp, sizeof(resp), 0) != sizeof(resp)) {
    perror("Server: send GET_MEM_SIZE_RESP error");
  }
}

void handle_write_req(int client_fd, const cxl_ipc_write_req_t *req,
                      server_state_t *state) {
  cxl_ipc_write_resp_t resp;
  resp.type = CXL_MSG_TYPE_WRITE_RESP;
  resp.status = CXL_IPC_STATUS_ERROR_GENERIC;

  CXL_SWITCH_SERVER_DEBUG_PRINT("Server: received write request, addr: %" PRIu64
                                ", size: %u, value: "
                                "0x%" PRIx64 "\n",
                                req->addr, req->size, req->value);

  if ((req->addr + req->size) > state->config.replicated_mem_size) {
    CXL_SWITCH_SERVER_DEBUG_PRINT("Server: write request out of bounds");
    resp.status = CXL_IPC_STATUS_ERROR_OUT_OF_BOUNDS;
    send(client_fd, &resp, sizeof(resp), 0);
    return;
  }

  /** Perform replicated write */

  pthread_mutex_lock(&state->mutex);
  int successful_writes = 0;
  int healthy_backends_found = 0;

  for (int i = 0; i < NUM_REPLICAS; i++) {
    if (state->replica_status[i] == CXL_IPC_STATUS_OK &&
        state->replica_mmap_addrs[i] != MAP_FAILED) {
      healthy_backends_found++;
      uint8_t *target_addr = state->replica_mmap_addrs[i] + req->addr;
      CXL_SWITCH_SERVER_DEBUG_PRINT("Writing to replica %d at host addr %p\n",
                                    i, target_addr);
      bool write_ok = true;
      switch (req->size) {
      case 1:
        *((uint8_t *)target_addr) = (uint8_t)req->value;
        break;
      case 2:
        *((uint16_t *)target_addr) = (uint16_t)req->value;
        break; // Assumes host and guest endianness match for mmap
      case 4:
        *((uint32_t *)target_addr) = (uint32_t)req->value;
        break; // or use stw_le_p equivalent if needed
      case 8:
        *((uint64_t *)target_addr) = req->value;
        break;
      default:
        CXL_SWITCH_SERVER_DEBUG_PRINT(
            "Unsupported write size %u to replica %d\n", req->size, i);
        write_ok = false; // Mark this replica as failed for this write?
        // For now, we just don't count it as successful.
        // A more robust system might mark the replica as unhealthy.
        // state->replica_health[i] = CXL_IPC_STATUS_ERROR_IO;
        break;
      }
      if (write_ok) {
        successful_writes++;
      }
    }
  }

  pthread_mutex_unlock(&state->mutex);

  if (healthy_backends_found == 0) {
    resp.status = CXL_IPC_STATUS_ERROR_NO_HEALTHY_BACKEND;
    CXL_SWITCH_SERVER_DEBUG_PRINT(
        "Server: no healthy backends found for write request\n");
  } else if (successful_writes == healthy_backends_found) {
    resp.status = CXL_IPC_STATUS_OK;
  } else { // partial success
    CXL_SWITCH_SERVER_DEBUG_PRINT(
        "Server: partial success, %d out of %d replicas written\n",
        successful_writes, healthy_backends_found);
    resp.status = CXL_IPC_STATUS_ERROR_IO;
  }

  if (send(client_fd, &resp, sizeof(resp), 0) != sizeof(resp)) {
    perror("Server: send WRITE_RESP error");
  }
}

void handle_read_req(int client_fd, const cxl_ipc_read_req_t *req,
                     server_state_t *state) {
  cxl_ipc_read_resp_t resp;
  resp.type = CXL_MSG_TYPE_READ_RESP;
  resp.status = CXL_IPC_STATUS_ERROR_GENERIC;
  resp.value = ~0ULL;

  CXL_SWITCH_SERVER_DEBUG_PRINT("Server: received read request, addr: %" PRIu64
                                ", size: %u\n",
                                req->addr, req->size);

  if ((req->addr + req->size) > state->config.replicated_mem_size) {
    CXL_SWITCH_SERVER_DEBUG_PRINT("Server: read request out of bounds");
    resp.status = CXL_IPC_STATUS_ERROR_OUT_OF_BOUNDS;
    send(client_fd, &resp, sizeof(resp), 0);
    return;
  }

  pthread_mutex_lock(&state->mutex);
  int replica_to_read = -1;
  // Read from the first healthy replica
  // A more complicated prototype might read from the closest numa node
  for (int i = 0; i < NUM_REPLICAS; i++) {
    if (state->replica_status[i] == CXL_IPC_STATUS_OK &&
        state->replica_mmap_addrs[i] != MAP_FAILED) {
      replica_to_read = i;
      break;
    }
  }

  if (replica_to_read != -1) {
    uint8_t *source_addr =
        state->replica_mmap_addrs[replica_to_read] + req->addr;
    CXL_SWITCH_SERVER_DEBUG_PRINT("Reading from replica %d at host addr %p\n",
                                  replica_to_read, source_addr);
    switch (req->size) {
    case 1:
      resp.value = *((uint8_t *)source_addr);
      break;
    case 2:
      resp.value = *((uint16_t *)source_addr);
      break;
    case 4:
      resp.value = *((uint32_t *)source_addr);
      break;
    case 8:
      resp.value = *((uint64_t *)source_addr);
      break;
    default:
      CXL_SWITCH_SERVER_DEBUG_PRINT(
          "Unsupported read size %u from replica %d\n", req->size,
          replica_to_read);
      resp.status = CXL_IPC_STATUS_ERROR_INVALID_REQ;
      break;
    }
    if (resp.status != CXL_IPC_STATUS_ERROR_INVALID_REQ) {
      resp.status = CXL_IPC_STATUS_OK;
    }
  } else {
    resp.status = CXL_IPC_STATUS_ERROR_NO_HEALTHY_BACKEND;
    CXL_SWITCH_SERVER_DEBUG_PRINT(
        "Server: no healthy backends found for read request\n");
  }
  pthread_mutex_unlock(&state->mutex);
  if (send(client_fd, &resp, sizeof(resp), 0) != sizeof(resp)) {
    perror("Server: send READ_RESP error");
  }
}

void handle_client_request(int client_fd, server_state_t *state) {
  uint8_t msg_type_header;
  ssize_t n;

  // Read just the type first
  n = recv(client_fd, &msg_type_header, sizeof(msg_type_header), MSG_PEEK);
  if (n <= 0) {
    if (n < 0)
      perror("Server: recv peek failed");
    else
      CXL_SWITCH_SERVER_DEBUG_PRINT("Server: client disconnected\n");
    return;
  }

  CXL_SWITCH_SERVER_DEBUG_PRINT(
      "Server: received message type header: %u, fd: %d\n", msg_type_header,
      client_fd);

  switch (msg_type_header) {
  case CXL_MSG_TYPE_GET_MEM_SIZE_REQ:
    CXL_SWITCH_SERVER_DEBUG_PRINT("Server: GET_MEM_SIZE_REQ\n");
    cxl_ipc_get_mem_size_req_t req;
    n = recv(client_fd, &req, sizeof(req), MSG_WAITALL);
    if (n == sizeof(req)) {
      handle_get_mem_size(client_fd, state);
    } else {
      CXL_SWITCH_SERVER_DEBUG_PRINT(
          "Server: GET_MEM_SIZE_REQ recv error, expected %zu bytes, got %zd\n",
          sizeof(req), n);
    }
    break;
  case CXL_MSG_TYPE_WRITE_REQ:
    CXL_SWITCH_SERVER_DEBUG_PRINT("Server: WRITE_REQ\n");
    cxl_ipc_write_req_t write_req;
    n = recv(client_fd, &write_req, sizeof(write_req), MSG_WAITALL);
    if (n == sizeof(write_req)) {
      handle_write_req(client_fd, &write_req, state);
    } else {
      CXL_SWITCH_SERVER_DEBUG_PRINT(
          "Server: WRITE_REQ recv error, expected %zu bytes, got %zd\n",
          sizeof(write_req), n);
    }
    break;
  case CXL_MSG_TYPE_READ_REQ:
    CXL_SWITCH_SERVER_DEBUG_PRINT("Server: READ_REQ\n");
    cxl_ipc_read_req_t read_req;
    n = recv(client_fd, &read_req, sizeof(read_req), MSG_WAITALL);
    if (n == sizeof(read_req)) {
      handle_read_req(client_fd, &read_req, state);
    } else {
      CXL_SWITCH_SERVER_DEBUG_PRINT(
          "Server: READ_REQ recv error, expected %zu bytes, got %zd\n",
          sizeof(read_req), n);
    }
    break;
  default:
    CXL_SWITCH_SERVER_DEBUG_PRINT(
        "Server: unknown message type %u. Draining a bit.\n", msg_type_header);
    // Drain a bit to prevent tight loop on unknown message, then close.
    char dummy_buf[256];
    recv(client_fd, dummy_buf, sizeof(dummy_buf), 0);
    cxl_ipc_error_resp_t error_resp = {.type = CXL_MSG_TYPE_ERROR_RESP,
                                       .status =
                                           CXL_IPC_STATUS_ERROR_INVALID_REQ};
    send(client_fd, &error_resp, sizeof(error_resp), 0);
    break;
  }
}

int main(int argc, char *argv[]) {
  if (argc < (1 + 1 + 1 + NUM_REPLICAS)) {
    fprintf(stderr,
            "Usage: %s <socket_path> <replica_size_MiB> <replica_path_1> ... "
            "<replica_path_N>\n",
            argv[0]);
    return EXIT_FAILURE;
  }

  // Zero init server_state
  memset(&server_state, 0, sizeof(server_state));

  server_state.config.socket_path = argv[1];
  server_state.config.replicated_mem_size =
      (uint64_t)atoi(argv[2]) * 1024 * 1024; // Convert MiB to bytes
  for (int i = 0; i < NUM_REPLICAS; i++) {
    server_state.config.replica_paths[i] = argv[3 + i];
    server_state.replica_fds[i] = -1;
    server_state.replica_mmap_addrs[i] = MAP_FAILED;
  }

  CXL_SWITCH_SERVER_DEBUG_PRINT(
      "Server starting. Socket: %s, Size: %" PRIu64 " bytes\n",
      server_state.config.socket_path, server_state.config.replicated_mem_size);
  for (int i = 0; i < NUM_REPLICAS; i++) {
    CXL_SWITCH_SERVER_DEBUG_PRINT("Replica %d path: %s\n", i,
                                  server_state.config.replica_paths[i]);
  }

  if (pthread_mutex_init(&server_state.mutex, NULL)) {
    perror("Failed to initialize mutex");
    return 1;
  }

  if (init_replicas(&server_state)) {
    fprintf(stderr, "Failed to initialize replicas\n");
    pthread_mutex_destroy(&server_state.mutex);
    return 1;
  }

  int listen_fd;
  struct sockaddr_un addr;

  listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    perror("Server: socket error");
    cleanup_replicas(&server_state);
    pthread_mutex_destroy(&server_state.mutex);
    return 1;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, server_state.config.socket_path,
          sizeof(addr.sun_path) - 1);
  unlink(server_state.config.socket_path); // Remove any existing socket

  if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    perror("Server: bind error");
    close(listen_fd);
    cleanup_replicas(&server_state);
    pthread_mutex_destroy(&server_state.mutex);
    return 1;
  }

  if (listen(listen_fd, MAX_CLIENTS) < 0) {
    perror("Server: listen error");
    close(listen_fd);
    cleanup_replicas(&server_state);
    pthread_mutex_destroy(&server_state.mutex);
    return 1;
  }

  CXL_SWITCH_SERVER_DEBUG_PRINT("Server listening on %s\n",
                                server_state.config.socket_path);

  fd_set active_fd_set, read_fd_set;
  FD_ZERO(&active_fd_set);
  FD_SET(listen_fd, &active_fd_set);
  int max_fd = listen_fd;

  while (true) {
    read_fd_set = active_fd_set;
    CXL_SWITCH_SERVER_DEBUG_PRINT("Calling select(), max_fd = %d\n", max_fd);

    int activity = select(max_fd - 1, &read_fd_set, NULL, NULL, NULL);
    if ((activity < 0) && (errno != EINTR)) {
      perror("Server: select error");
      break;
    }

    if (FD_ISSET(listen_fd, &read_fd_set)) { // New connection
      int client_fd = accept(listen_fd, NULL, NULL);
      if (client_fd < 0) {
        perror("Server: accept error");
      } else {
        CXL_SWITCH_SERVER_DEBUG_PRINT(
            "Server: accepted new connection, fd = %d\n", client_fd);
        FD_SET(client_fd, &active_fd_set);
        if (client_fd > max_fd) {
          max_fd = client_fd;
        }
      }
    }

    // Check existing clients for incoming data
    for (int i = 0; i <= max_fd; i++) {
      if (i != listen_fd && FD_ISSET(i, &read_fd_set)) {
        CXL_SWITCH_SERVER_DEBUG_PRINT("Server: activity on fd = %d\n", i);
        // Assume just one request per activity for now.
        // A more robust impl shud check for partial reads/writes.
        char peek_buf;
        ssize_t peek_ret = recv(i, &peek_buf, 1, MSG_PEEK | MSG_DONTWAIT);

        if (peek_ret > 0) { // Data available
          handle_client_request(i, &server_state);
        } else if (peek_ret == 0) { // Client disconnected
          CXL_SWITCH_SERVER_DEBUG_PRINT(
              "Server: client disconnected, fd = %d\n", i);
          close(i);
          FD_CLR(i, &active_fd_set);
          // recalculate max_fd if i was max_fd
          if (i == max_fd) {
            while (max_fd > listen_fd && !FD_ISSET(max_fd, &active_fd_set)) {
              max_fd--;
            }
          }
        } else { // Error
          if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("Server: recv error on client fd");
            CXL_SWITCH_SERVER_DEBUG_PRINT(
                "Server: client error, closing fd = %d\n", i);
            close(i);
            FD_CLR(i, &active_fd_set);
            if (i == max_fd) {
              while (max_fd > listen_fd && !FD_ISSET(max_fd, &active_fd_set)) {
                max_fd--;
              }
            }
          }
          // If EAGAIN or EWOULDBLOCK, no data right now, select will
          // handle it later (hopefully)
        }
      }
    }
  }

  CXL_SWITCH_SERVER_DEBUG_PRINT("Server: shutting down\n");
  close(listen_fd);

  // Close all active client sockets
  for (int i = 0; i <= max_fd; i++) {
    if (FD_ISSET(i, &active_fd_set) && i != listen_fd) {
      close(i);
    }
  }

  cleanup_replicas(&server_state);
  pthread_mutex_destroy(&server_state.mutex);
  unlink(server_state.config.socket_path); // Clean up socket file

  return 0;
}