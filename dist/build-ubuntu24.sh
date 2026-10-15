#!/usr/bin/env bash
set -euo pipefail
# Check in a subshell: os-release's VERSION must not overwrite package VERSION.
( . /etc/os-release; [[ $ID == ubuntu && $VERSION_ID == 24.04 ]] ) || { echo 'Ubuntu 24.04 build environment required' >&2; exit 1; }
bash /priv/dist/build-22.04.sh
bash /priv/dist/bundle-libs.sh
bash /priv/dist/repack-container.sh
