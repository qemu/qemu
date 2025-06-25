#ifndef RPC_INTERFACE_HPP
#define RPC_INTERFACE_HPP

#include <functional>
#include <string>
namespace diancie {

// Base template for RPC function traits to ensure type safety.
template <typename FunctionEnum, FunctionEnum func_id>
struct DiancieFunctionTraits;

// Macro to define RPC function signatures
#define DEFINE_DIANCIE_FUNCTION(enum_type, func_id, return_type, ...)          \
  namespace diancie {                                                          \
  template <> struct DiancieFunctionTraits<enum_type, enum_type::func_id> {    \
    using ReturnType = return_type;                                            \
    using ArgsTuple = std::tuple<__VA_ARGS__>;                                 \
    static constexpr auto function_id = enum_type::func_id;                    \
    static constexpr const char* name = #func_id;                                               \
  };                                                                           \
  }

struct FunctionInfo {
  std::function<void(void *args_region, void *result_region)> handler;
  size_t args_size;
  size_t result_size;
  size_t args_alignment;
  size_t result_alignment;
  std::string name; // Only for debugging
};

} // namespace diancie

#endif