#!/usr/bin/env bash
set -euo pipefail
# Developer asset conversion only; ImageMagick is not an application dependency.
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
convert "$repo_dir/Logo/VelocityCopy.png" \
  -define icon:auto-resize=256,128,96,80,64,48,40,32,28,24,20,16 \
  "$repo_dir/src/ui/VelocityCopy.UI/Assets/VelocityCopy.ico"
