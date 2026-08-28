#!/usr/bin/env bash
#
# fixll — optional source rebuild (cross-distro)
#
# The shipped VERSION.dll is already built. This script only matters if you
# want to recompile from src/ yourself. It installs a 32-bit MinGW-w64
# toolchain via your distro's package manager, then compiles VERSION.dll.
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR/src"

CC="i686-w64-mingw32-g++"

if ! command -v "$CC" >/dev/null 2>&1; then
  echo "==> No 32-bit MinGW found. Installing via package manager..."
  if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update -y && sudo apt-get install -y mingw-w64
  elif command -v dnf >/dev/null 2>&1; then
    sudo dnf install -y mingw32-gcc-c++
  elif command -v pacman >/dev/null 2>&1; then
    sudo pacman -S --needed --noconfirm mingw-w64
  elif command -v zypper >/dev/null 2>&1; then
    sudo zypper install -y mingw32-gcc-c++
  elif command -v apk >/dev/null 2>&1; then
    sudo apk add mingw-w64-gcc
  elif command -v nix-build >/dev/null 2>&1 || command -v nix >/dev/null 2>&1; then
    echo "    NixOS users: 'nix build nixpkgs#pkgsCross.mingw32.stdenv.cc' then use"
    echo "    the resulting bin/i686-w64-mingw32-g++. (Or just use the prebuilt DLL.)"
    exit 1
  else
    echo "ERROR: unsupported package manager. Install mingw-w64 (i686) manually."
    exit 1
  fi
fi

echo "==> Compiling VERSION.dll (32-bit)"
"$CC" -shared -m32 -O2 -DUNICODE -D_UNICODE \
  -I. -I./detours dllmain.cpp detours/detours.cpp detours/disasm.cpp detours/modules.cpp \
  VERSION.def \
  -Wl,--enable-stdcall-fixup -lshlwapi -lpsapi -static -o "$SCRIPT_DIR/VERSION.dll"

echo "==> Done -> $SCRIPT_DIR/VERSION.dll"
