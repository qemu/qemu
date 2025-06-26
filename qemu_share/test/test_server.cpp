#include "../includes/test_interface.hpp"
#include "../serverlib/rpcserver.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <numeric>
#include <thread>

using namespace diancie;

// Business logic functions - work directly with shared memory
int add_numbers_impl(int &a, int &b) {
  std::cout << "Server: Adding " << a << " + " << b << " (zero-copy)"
            << std::endl;
  int result = a + b;
  std::cout << "Server: Result = " << result << std::endl;
  return result;
}

int multiply_doubles_impl(int &a, int &b) {
  std::cout << "Server: Multiplying " << a << " * " << b << " (zero-copy)"
            << std::endl;
  int result = a * b;
  std::cout << "Server: Result = " << result << std::endl;
  return result;
}

Person process_person(Person &person1, Person &person2) {
  std::cout << "Server: Processing person data (zero-copy)" << std::endl;
  std::cout << "Person 1: , Age: " << person1.age << ", Salary: $"
            << person1.salary << std::endl;
  std::cout << "Person 2: , Age: " << person2.age << ", Salary: $"
            << person2.salary << std::endl;

  Person stats;
  stats.age = person1.age + person2.age;
  stats.salary = person1.salary + person2.salary;
  stats.kill_count = 2;

  std::cout << "Server: Combined stats - Total Age: " << stats.age
            << ", Total Salary: $" << stats.salary
            << ", Count: " << stats.kill_count << std::endl;

  return stats;
}

int main(int argc, char *argv[]) {
  try {
    const std::string device_path = "/dev/cxl_switch_client0";
    const std::string service_name = "TestService1";
    const std::string instance_id = "ClientInstance1";

    DiancieServer<TestServiceFunctions> server(device_path, service_name,
                                               instance_id);

    std::cout << "\n=== Registering RPC Functions ===" << std::endl;

    server.register_rpc_function<TestServiceFunctions::ADD>(add_numbers_impl);
    server.register_rpc_function<TestServiceFunctions::MULTIPLY>(
        multiply_doubles_impl);
    server.register_rpc_function<TestServiceFunctions::PERSON>(process_person);

    std::cout << "\n=== Registering Service ===" << std::endl;
    if (!server.register_service()) {
      std::cerr << "Failed to register service!" << std::endl;
      return 1;
    }

    std::cout << "\n=== Starting Server Loop ===" << std::endl;
    std::cout << "Server ready to accept clients..." << std::endl;

    server.run_server_loop();

  } catch (const std::exception &e) {
    std::cerr << "Server error: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}