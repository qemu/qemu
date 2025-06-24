#ifndef A_CXL_CONNECTOR_HPP
#define A_CXL_CONNECTOR_HPP

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdint.h>
#include <string>
namespace diancie {

/// An abstract CXL connection wraps around the @DiancieHeap which is
/// represented as the following:
// ┌───────────────┐◄── Client/Request Queue Offset
// │  Client Area  │
// ├───────────────┤◄── Server/Response Queue Offset
// │  Server Area  │
// ├───────────────┤◄── Data Area Offset
// │               │
// │               │
// │  Data Area    │
// │               │
// │               │
// │               │
// └───────────────┘◄── 
///
/// Both server and client queues are 64-bit
/// queue entries or @QueueEntry which are essentially ring buffers.
/// Each 64-bit entry consists of 1 bit for the flag and 63 bit for the
/// address into the Shm Heap.
/// TODO: Integrate the WAL log into the Shm region.
/// TODO: Can we use HydraRPC's optimized polling? 
///          (intel instructions umonitor and uwait)
///       Would this work for CXL memory technically?
///       - Client/server can issue a `monitor` instruction at the cache line 
///         granularity for the circular buffer.
///       - Subsequently, a `uwait` instruction is executed to halt the CPU and
///         preserve power.
///       - The CPU is woken up when the monitored data is modified
///         
/// TODO: MSI is enabled in CXL 3.0 so make use of that

/// Abstraction for an entry in the CXLQueue, for either the server or client.
struct QueueEntry {
private:
  static constexpr uint64_t FLAG_MASK = 0x8000000000000000ULL;
  static constexpr uint64_t ADDRESS_MASK = 0x7FFFFFFFFFFFFFFFULL;
  
  uint64_t data;

public:
  QueueEntry() : data(0) {}
  explicit QueueEntry(uint64_t value) : data(value) {}
  
  // Get the flag bit (MSB)
  bool get_flag() const {
    return (data & FLAG_MASK) != 0;
  }
  
  // Set the flag bit
  void set_flag(bool flag) {
    if (flag) {
      data |= FLAG_MASK;
    } else {
      data &= ADDRESS_MASK;
    }
  }
  
  // Get the address (lower 63 bits)
  uint64_t get_address() const {
    return data & ADDRESS_MASK;
  }
  
  // Set the address (lower 63 bits)
  void set_address(uint64_t address) {
    data = (data & FLAG_MASK) | (address & ADDRESS_MASK);
  }
  
  // Get raw data
  uint64_t get_raw() const {
    return data;
  }
};

/// Abstraction for the client/server request/response Queue.
template<size_t Size>
class CXLQueue {
private:
  static_assert(Size > 0, "CXLQueue size must be greater than 0");
  static_assert((Size & (Size - 1)) == 0, "CXLQueue size must be a power of 2");

  QueueEntry entries_[Size];
public:
  CXLQueue() = default;

  CXLQueue(const CXLQueue&) = delete;
  CXLQueue& operator=(const CXLQueue&) = delete;

  CXLQueue(CXLQueue&&) = delete;
  CXLQueue& operator=(CXLQueue&&) = delete;
};


/// Each client/server that connects via the CXL connection maintains
/// their own view of the DiancieHeap. The DiancieHeap is just a reinterpret_cast
/// over the large memory region anyways ?
/// TODO: Figure out how to let a server transparently take over?
/// Idea: Enumerate the fixed offsets for server/client queue
class DiancieHeap {
private:

public:
  // Each queue entry is 64 bits, or 8 bytes.
  static constexpr int NUM_QUEUE_ENTRIES = 128;
  static constexpr uint64_t CLIENT_QUEUE_OFFSET = 0;
  static constexpr size_t   CLIENT_QUEUE_SIZE   = NUM_QUEUE_ENTRIES * 8;

  static constexpr uint64_t SERVER_QUEUE_OFFSET = 0 + CLIENT_QUEUE_SIZE;
  static constexpr uint64_t SERVER_QUEUE_SIZE   = NUM_QUEUE_ENTRIES * 8;

  static constexpr uint64_t DATA_AREA_OFFSET    = 0 + CLIENT_QUEUE_SIZE + SERVER_QUEUE_SIZE;

  size_t size;
  uint64_t DATA_AREA_SIZE    = size - CLIENT_QUEUE_SIZE + SERVER_QUEUE_SIZE;
public:
  DiancieHeap() = default;
  DiancieHeap(size_t size);
  ~DiancieHeap();
};

/// Represents an abstract CXL shm connection between a client and server.
/// Intended for emulation and actual interfacing.
struct AbstractCXLConnection {
public:
  AbstractCXLConnection() = default;
  ~AbstractCXLConnection() = default;

  virtual uint64_t get_base() = 0;
  virtual uint64_t get_size() = 0;
  virtual uint64_t get_channel_id() = 0;
};

enum class CXLEvent {
  NEW_CLIENT_CONNECTED,
  CLIENT_DISCONNECTED,
  CHANNEL_CLOSED,
  COMMAND_RECEIVED,
  ERROR_OCURRED
};

struct CXLEventData {
  CXLEvent type;
  uint64_t channel_id;
  std::unique_ptr<AbstractCXLConnection> connection;
  std::string error_message;
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
  virtual std::optional<CXLEventData> wait_for_event(int timeout_ms) = 0;
  virtual bool wait_for_command_response(int timeout_ms) = 0;
  virtual uint32_t get_notification_status() = 0;
  virtual bool send_command(const void* req, size_t size) = 0;
  virtual bool recv_response(void* resp, size_t size) = 0;
  // In the real deployment, must configure the window (if CXL: it wud be the logical view)
  virtual bool set_memory_window(uint64_t offset, uint64_t size, uint64_t channel_id) = 0;
};

}; // namespace diancie


#endif