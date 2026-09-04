#!/bin/bash
apt-get install -y -qq cmake libboost-dev libboost-regex-dev libboost-filesystem-dev libboost-system-dev >/dev/null 2>&1
cd /src/test-boost
rm -rf build
cmake -S . -B build 2>&1 | grep -iE 'Boost|Error|Found'
