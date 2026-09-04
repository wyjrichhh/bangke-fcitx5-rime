#!/bin/bash
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq libboost-regex-dev libboost-filesystem-dev libboost-system-dev libboost-locale-dev 2>&1 | tail -3
echo "=== regex lib ==="
ls -la /usr/lib/x86_64-linux-gnu/libboost_regex.so* 2>&1
echo "=== filesystem lib ==="
ls -la /usr/lib/x86_64-linux-gnu/libboost_filesystem.so* 2>&1
