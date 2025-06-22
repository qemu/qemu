#!/bin/bash

g++ -std=c++17 ../includes/qemu_cxl_connector.cpp rpcserver.cpp concurrent_server.cpp -o ccserver
