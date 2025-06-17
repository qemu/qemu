#!/bin/bash

g++ -std=c++17 rpcserver.cpp diancie_main.cpp -o rpcserver
g++ -std=c++17 rpcserver.cpp concurrent_server.cpp -o ccserver