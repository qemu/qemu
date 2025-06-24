#!/bin/bash

echo "Building RPC Test Programs..."

# Build server
echo "Building test server..."
g++ -std=c++17 -O0 -g \
    -I../includes \
    -I../serverlib \
    -I. \
    test_server.cpp \
    ../includes/*.cpp \
    -lpthread \
    -o test_server

# Build client  
echo "Building test client..."
g++ -std=c++17 -O0 -g \
    -I../includes \
    -I../clientlib \
    -I. \
    test_client.cpp \
    ../includes/*.cpp \
    -lpthread \
    -o test_client

echo "Build complete!"