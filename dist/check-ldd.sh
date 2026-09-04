#!/bin/bash
for f in /src/librime/build/lib/librime.so.1.16.1 /src/librime/build/lib/rime-plugins/librime-ai-predict.so /src/fcitx5-rime-5.0.11/build/src/rime.so; do
  echo "=== $(basename $f) ==="
  ldd "$f" 2>&1 | grep -oE '(libboost|libyaml|libglog|libgflags|libleveldb|libmarisa|libopencc|libsnappy|librime)[^ ]*\.so[.0-9]*' | sort -u
done
