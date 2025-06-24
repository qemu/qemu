#ifndef TEST_RPC_INTERFACE_HPP
#define TEST_RPC_INTERFACE_HPP

#include "rpc_interface.hpp"

using namespace diancie;

struct Person {
  int age;
  int salary; // avoid double for now
  int kill_count;
};

enum class TestServiceFunctions : uint32_t {
  ADD,
  MULTIPLY,
  PERSON,
};

DEFINE_DIANCIE_FUNCTION(TestServiceFunctions, ADD, int, int, int)
DEFINE_DIANCIE_FUNCTION(TestServiceFunctions, MULTIPLY, int, int, int)
DEFINE_DIANCIE_FUNCTION(TestServiceFunctions, PERSON, Person, Person, Person)

#endif