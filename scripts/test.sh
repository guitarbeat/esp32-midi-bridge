#!/bin/bash
set -e

echo "Running unit tests..."
mkdir -p build/test

# MidiCodecTest
g++ -I./test/mock -I./firmware/bridge-s3 test/MidiCodecTest.cpp -o build/test/MidiCodecTest
./build/test/MidiCodecTest

echo "All tests passed successfully!"
