@echo off
rem Builds SW2Widescreen.asi (32-bit DLL) with Zig (https://ziglang.org, "zig" in PATH)
rem or MinGW-w64 32-bit (MSYS2 "MINGW32" shell). Both use the standard MinGW C runtime.
cd /d "%~dp0"
where zig >nul 2>&1 && (
  zig cc -target x86-windows-gnu -O2 -s -shared -o SW2Widescreen.asi sw2ws.c version.rc -lkernel32 -luser32 -Wl,--subsystem,windows || goto :err
  goto :ok
)
windres -F pe-i386 version.rc -O coff -o version.o || goto :err
i686-w64-mingw32-gcc -O2 -s -shared -o SW2Widescreen.asi sw2ws.c version.o -lkernel32 -luser32 || goto :err
:ok
echo OK: SW2Widescreen.asi
exit /b 0
:err
echo Build failed.
exit /b 1
