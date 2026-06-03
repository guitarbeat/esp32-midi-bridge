#!/bin/bash
set -e

echo "Running unit tests..."
mkdir -p build/test

# MidiCodecTest
g++ -std=c++11 -I./test/mock -I./firmware/bridge-s3/src -I./firmware/bridge-s3/src/midi test/MidiCodecTest.cpp -o build/test/MidiCodecTest
./build/test/MidiCodecTest

# MidiEngineTest
g++ -std=c++11 -I./test/mock -I./firmware/bridge-s3/src -I./firmware/bridge-s3/src/midi test/MidiEngineTest.cpp firmware/bridge-s3/src/midi/MidiEngine.cpp -o build/test/MidiEngineTest
./build/test/MidiEngineTest

# MidiBridgeTest
g++ -std=c++11 -I./test/mock -I./firmware/bridge-s3/src -I./firmware/bridge-s3/src/midi test/MidiBridgeTest.cpp test/shims/BridgeUiNotifyStub.cpp firmware/bridge-s3/src/midi/MidiBridge.cpp firmware/bridge-s3/src/midi/MidiEngine.cpp -o build/test/MidiBridgeTest
./build/test/MidiBridgeTest

echo "All tests passed successfully!"
