#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "../includes/cxl_switch_ipc.h"

void print_usage(const char *prog_name) {
  std::cerr << "Usage: " << prog_name << " fail <replica_index>" << std::endl;
}

int main(int argc, char* argv[]) {
  if (argc < 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  std::string admin_socket_path = CXL_SWITCH_SERVER_ADMIN_SOCKET_PATH_DEFAULT;
  
  if (std::string(argv[1]) != "fail") {
    std::cerr << "Unknown command " << argv[1] << std::endl;
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }
  
  cxl_admin_fail_replica_req_t req;
  cxl_admin_fail_replica_resp_t resp;
  
  req.cmd_type = CXL_ADMIN_CMD_TYPE_FAIL_REPLICA;
  req.memdev_index = static_cast<uint8_t>(std::stoi(argv[2]));
  
  int sockfd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (sockfd < 0) {
    std::cerr << "Failure to create socket" << std::endl;
    return EXIT_FAILURE;
  }

  struct sockaddr_un addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  std::strncpy(addr.sun_path, admin_socket_path.c_str(), sizeof(addr.sun_path) - 1);
  addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

  std::cout << "Connecting to admin socket ... " << std::endl;

  if (::connect(sockfd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::cerr << "Failed to connect to admin socket" << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "Connected to admin socket." << std::endl;

  std::cout << "Sending admin command " << req.cmd_type << std::endl;
  
  ssize_t bytes_sent = ::send(sockfd, &req, sizeof(req), 0);
  if (bytes_sent < 0) {
    std::cerr << "Failed to send admin command: " << strerror(errno) << std::endl;
    ::close(sockfd);
    return EXIT_FAILURE;
  } else if (bytes_sent != sizeof(req)) {
    std::cerr << "Partial send, expected " << sizeof(req) << " bytes, sent " << bytes_sent << " bytes." << std::endl;
    ::close(sockfd);
    return EXIT_FAILURE;
  }

  ssize_t bytes_recv = ::recv(sockfd, &resp, sizeof(resp), MSG_WAITALL);
  if (bytes_recv < 0) {
    std::cerr << "Failed to receive admin response: " << strerror(errno) << std::endl;
    ::close(sockfd);
    return EXIT_FAILURE;
  } else if (bytes_recv != sizeof(resp)) {
    std::cerr << "Partial receive, expected " << sizeof(resp) << " bytes, received " << bytes_recv << " bytes." << std::endl;
    ::close(sockfd);
    return EXIT_FAILURE;
  }

  std::cout << "Admin command response status: " << static_cast<int>(resp.status) << std::endl;

  switch(resp.status) {
    case CXL_IPC_STATUS_OK:
      std::cout << "Replica " << static_cast<int>(req.memdev_index) << " failed successfully." << std::endl;
      break;
    case CXL_IPC_STATUS_ERROR_INVALID_REQ:
      std::cerr << "Invalid request for failing replica." << std::endl;
      break;
    case CXL_IPC_STATUS_ERROR_GENERIC:
      std::cerr << "Generic error while failing replica." << std::endl;
      break;
    default:
      std::cerr << "Unknown status received: " << static_cast<int>(resp.status) << std::endl;
  }

  close(sockfd);
  return (resp.status == CXL_IPC_STATUS_OK) ? EXIT_SUCCESS : EXIT_FAILURE;
}