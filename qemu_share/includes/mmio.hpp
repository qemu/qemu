#ifndef MMIO_SAFE_HPP
#define MMIO_SAFE_HPP

#include <cstring>
#include <type_traits>
namespace diancie {
template <typename T> class MMIOSafe {
private:
  template <typename U> static constexpr bool is_mmio_safe() {
    // Allow trivially copyable types
    if constexpr (std::is_trivially_copyable_v<U>) {
      return true;
    }
    // Allow standard layout types that are safe for memcpy
    else if constexpr (std::is_standard_layout_v<U> &&
                       std::is_trivially_destructible_v<U>) {
      return true;
    }
    // Specifically allow std::tuple of safe types (common case)
    else if constexpr (std::is_same_v<U, std::tuple<int, int>> ||
                       std::is_same_v<U, std::tuple<double, double>> ||
                       std::is_same_v<U, std::tuple<int, double>>) {
      return true;
    } else {
      return false;
    }
  }

  static_assert(is_mmio_safe<T>, "Type is not safe for MMIO operations");

  template <typename U> static constexpr bool has_floating_point() {
    if constexpr (std::is_floating_point_v<U>) {
      return true;
    } else if constexpr (std::is_class_v<U> || std::is_union_v<U>) {
      // Conservative true for classes/structs
      return true;
    } else {
      return false;
    }
  }

public:
  static void write(volatile void *mmio_addr, const T &value) {
    if constexpr (has_floating_point<T>()) {
      // Use memcpy for floating point types to avoid FP instructions
      std::memcpy(const_cast<void *>(mmio_addr), &value, sizeof(T));
    } else {
      // Direct assignment for non-floating point types
      *static_cast<volatile T *>(mmio_addr) = value;
    }
  }

  static T read(volatile void *mmio_addr) {
    if constexpr (has_floating_point<T>()) {
      // Use safe memcpy approach
      T result;
      std::memcpy(&result, const_cast<void *>(mmio_addr), sizeof(T));
      return result;
    } else {
      return *static_cast<volatile T *>(mmio_addr);
    }
  }
};

template <typename T> void mmio_write(volatile void *addr, const T &value) {
  MMIOSafe<T>::write(addr, value);
}

template <typename T> T mmio_read(volatile void *addr) {
  return MMIOSafe<T>::read(addr);
}

} // namespace diancie

#endif