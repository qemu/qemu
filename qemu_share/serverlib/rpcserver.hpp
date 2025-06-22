#ifndef DIANCIE_RPC_LIB_HPP
#define DIANCIE_RPC_LIB_HPP

#include "../includes/cxl_switch_ipc.h"
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "../includes/ioctl_defs.h"
#include "../includes/a_cxl_connector.hpp"
#include "../includes/qemu_cxl_connector.hpp"

namespace diancie {

// Represents an active, mmapped connection between the
// server instance and a client instance.
// Maintains the server's queue entry and the client's queue entry
// which are fixed size offsets
// Both queue entries are 64 bits, 1 bit for poll and 63 bits for indexing
// into the shared memory region heap

// Server Queue | Client Queue | Shared Memory Heap
struct QueueEntry {
  uint64_t queue_entry;
};


using ClientId = std::string;
using ChannelId = uint64_t;

class DiancieServer : protected QEMUCXLConnector {
public:
  // Constructors
  DiancieServer() = delete;
  
  DiancieServer(const std::string& device_path, const std::string& service_name, const std::string& instance_id);
  
  ~DiancieServer();

  DiancieServer(const DiancieServer&) = delete;
  DiancieServer& operator=(const DiancieServer&) = delete;

  DiancieServer(DiancieServer&&) = delete;
  DiancieServer& operator=(DiancieServer&&) = delete;

  void register_rpc_function();
  void service_client(QEMUConnection connection);
  void run_server_loop();

private:
  std::string service_name_;
  std::string instance_id_;
  std::unordered_map<uint64_t, std::thread> clients_;
  
private:
  void handle_new_client(std::unique_ptr<AbstractCXLConnection> conn);
  void handle_channel_close(uint64_t channel_id);
  void handle_client_disconnect(uint64_t channel_id);
  void service_client(std::unique_ptr<AbstractCXLConnection> conn);

  uint32_t get_command_status();
  // Called once in constructor
  bool register_service();
  // Called in destructor?
  void deregister_service();
};

}


#endif // DIANCIE_RPC_LIB_HPP