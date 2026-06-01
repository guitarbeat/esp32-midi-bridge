#!/bin/bash
set -e

echo "Running unit tests..."
mkdir -p build/test

# MidiCodecTest
g++ -std=c++11 -I./test/mock -I./firmware/bridge-s3 test/MidiCodecTest.cpp -o build/test/MidiCodecTest
./build/test/MidiCodecTest

# MidiEngineTest
g++ -std=c++11 -I./test/mock -I./firmware/bridge-s3 test/MidiEngineTest.cpp firmware/bridge-s3/MidiEngine.cpp -o build/test/MidiEngineTest
./build/test/MidiEngineTest

# MidiBridgeTest
g++ -std=c++11 -I./test/mock -I./firmware/bridge-s3 test/MidiBridgeTest.cpp test/shims/BridgeUiNotifyStub.cpp firmware/bridge-s3/MidiBridge.cpp firmware/bridge-s3/MidiEngine.cpp -o build/test/MidiBridgeTest
./build/test/MidiBridgeTest

echo "All tests passed successfully!"
