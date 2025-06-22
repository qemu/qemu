#ifndef DIANCIE_RPC_CLIENT_HPP
#define DIANCIE_RPC_CLIENT_HPP

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "../includes/cxl_switch_ipc.h"
#include "../includes/ioctl_defs.h"
#include "../includes/qemu_cxl_connector.hpp"
#include "../includes/a_cxl_connector.hpp"

namespace diancie {

// The memory region that the client library is allocated
struct ChannelInfo {
  uint64_t offset;
  uint64_t size;
  uint64_t channel_id;
};

enum class ClientState {
  DISCONNECTED,
  CONNECTING,
  CONNECTED,
  ERROR
};

// The client library manages the low-level interactions for the user program.
// 1. Discover service and get allocated channel
// 2. Run background thread for heartbeat and detecting that channel has closed
class DiancieClient : protected QEMUCXLConnector {
private:
  std::string service_name_;
  std::string instance_id_;
  uint64_t channel_id_;

  // Event loop management
  std::thread event_thread_;
  std::atomic_bool running_{false};
  std::atomic<ClientState> client_state_{ClientState::DISCONNECTED};
  // TODO: Heartbeat? But this should be QEMUCXLConnector

  mutable std::mutex state_mutex_;
  
public:
  DiancieClient() = delete;
  DiancieClient(const std::string& device_path, const std::string& service_name, const std::string& instance_id);
  ~DiancieClient();

  DiancieClient(const DiancieClient&) = delete;
  DiancieClient& operator=(const DiancieClient&) = delete;
  // For testing purposes, not final
  void client_write_u64(uint64_t offset, uint64_t value);
  uint64_t client_read_u64(uint64_t offset);

  // Conn management
  bool is_connected() const;
  ClientState get_state() const;
  void disconnect();
  bool reconnect();

private:
  // Setup and cleanup
  // Methods that are called by ctor and dtor
  // Find a service via the FM and request a comms channel
  std::optional<ChannelInfo> request_channel(const std::string& service_name, const std::string& instance_id);
  bool release_channel();
  void start_event_loop();
  void stop_event_loop();
  
  void client_event_loop();
  void handle_channel_close();

  void set_state(ClientState new_state);
};

}

#endif