#!/bin/sh
# Builds SW2Widescreen.asi (32-bit Windows DLL) with Zig (tested) or MinGW-w64.
set -e
cd "$(dirname "$0")"
if command -v zig >/dev/null 2>&1; then
  zig cc -target x86-windows-gnu -O2 -s -shared -o SW2Widescreen.asi sw2ws.c version.rc -lkernel32 -luser32 -Wl,--subsystem,windows
else
  i686-w64-mingw32-windres version.rc -O coff -o version.o
  i686-w64-mingw32-gcc -O2 -s -shared -o SW2Widescreen.asi sw2ws.c version.o -lkernel32 -luser32
fi
echo "OK: SW2Widescreen.asi"
