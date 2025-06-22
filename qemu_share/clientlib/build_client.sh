#!/bin/bash

g++ -std=c++17 -g ../includes/qemu_cxl_connector.cpp rpcclient.cpp client_main.cpp -o rpcclient