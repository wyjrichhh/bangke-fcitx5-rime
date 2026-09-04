#!/bin/bash
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq cmake ninja-build libboost-dev libboost-regex-dev libboost-filesystem-dev libboost-system-dev >/dev/null 2>&1
mkdir -p /tmp/tb
cat > /tmp/tb/CMakeLists.txt <<'EOF'
cmake_minimum_required(VERSION 3.13)
project(tb)
set(Boost_NO_BOOST_CMAKE ON)
set(Boost_USE_MULTITHREADED ON)
set(Boost_USE_STATIC_LIBS OFF)
find_package(Boost 1.74.0 REQUIRED COMPONENTS regex filesystem system)
message(STATUS "Boost_LIBRARIES=[${Boost_LIBRARIES}]")
message(STATUS "Boost_REGEX_LIBRARY=[${Boost_REGEX_LIBRARY}]")
message(STATUS "Boost_FILESYSTEM_LIBRARY=[${Boost_FILESYSTEM_LIBRARY}]")
message(STATUS "Boost_SYSTEM_LIBRARY=[${Boost_SYSTEM_LIBRARY}]")
EOF
cd /tmp/tb
cmake -S . -B build 2>&1 | grep -iE 'Boost_|Found Boost|Error'
echo "=== cmake 版本 ==="
cmake --version | head -1
echo "=== boost .so 实际存在 ==="
ls -la /usr/lib/x86_64-linux-gnu/libboost_regex.so /usr/lib/x86_64-linux-gnu/libboost_filesystem.so /usr/lib/x86_64-linux-gnu/libboost_system.so 2>&1
