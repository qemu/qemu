#ifndef A_CXL_CONNECTOR_HPP
#define A_CXL_CONNECTOR_HPP

#include <cstddef>
#include <stdint.h>
namespace diancie {

/// Represents an abstract CXL connection between a client and server.
/// Intended for emulation and actual interfacing
struct AbstractCXLConnection {
public:
  AbstractCXLConnection() = default;
  ~AbstractCXLConnection() = default;

  virtual uint64_t get_base() = 0;
  virtual uint64_t get_size() = 0;
  virtual uint64_t get_channel_id() = 0;
};

/// This abstract class is responsible for all low-level interactions with
/// the underlying CXL Switch device. It will make it possible to easily
/// (hopefully) transition between our current QEMU emulation for correctness
/// verification and a more faithful (but challenging to set up) configuration
class AbstractCXLConnector {
public:
  AbstractCXLConnector() = default;
  virtual ~AbstractCXLConnector() = default;
protected:
  virtual bool wait_for_command_response(int timeout_ms) = 0;
  virtual uint32_t get_notification_status() = 0;
  virtual bool send_command(const void* req, size_t size) = 0;
  virtual bool recv_response(void* resp, size_t size) = 0;
  // In the real deployment, must configure the window (if CXL: it wud be the logical view)
  virtual bool set_memory_window(uint64_t offset, uint64_t size) = 0;
};

}; // namespace diancie


#endif