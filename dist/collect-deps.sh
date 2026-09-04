#!/bin/bash
declare -A seen
collect() {
  local f="$1"
  [ -e "$f" ] || return
  local dep
  while read -r dep; do
    local p
    p=$(echo "$dep" | grep -oE '/[^ ]+\.so[.0-9]*' | head -1)
    [ -n "$p" ] || continue
    if [ -z "${seen[$p]}" ]; then
      seen[$p]=1
      collect "$p"
    fi
  done < <(ldd "$f" 2>/dev/null | grep -oE '/[^ ]+\.so[.0-9]*')
}
for f in /src/librime/build/lib/librime.so.1.16.1 /src/librime/build/lib/rime-plugins/librime-ai-predict.so /src/fcitx5-rime-5.0.11/build/src/rime.so; do
  collect "$f"
done
# 输出除核心系统库外的所有依赖
for p in "${!seen[@]}"; do
  base=$(basename "$p")
  case "$base" in
    libc.so*|libstdc++.so*|libm.so*|libpthread.so*|libdl.so*|librt.so*|libgcc_s.so*|ld-linux*|libfcitx*|libdbus*|libxkb*|libFcitx*) ;;
    *) echo "$p" ;;
  esac
done | sort
