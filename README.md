# Samurai Warriors 2 (PC) – Widescreen Fix

Source code of **SW2Widescreen.asi**, the plugin behind the Nexus Mods release
[Widescreen Fix 16:9 / 21:9](https://www.nexusmods.com/samuraiwarriors2/mods/5).

The PC port of Samurai Warriors 2 only offers 4:3 resolutions. This plugin, loaded at game start by
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (`dinput8.dll`, MIT license, by ThirteenAG),
patches the game **in memory only** (no game file is modified):

- replaces the resolution functions so the game runs at the desktop resolution (or the one set in `SW2Widescreen.ini`);
- keeps the 3D image unstretched;
- re-centers the 2D HUD and menus with correct proportions, sends menu side pieces to the screen edges (`hud.c`, Direct3D 9 hooks);
- keeps sound playing when the game is in the background (Alt+Tab), so no dialogue is lost.

## Files

| File | Role |
|---|---|
| `sw2ws.c` | Plugin entry point: waits for the protected `sw2.exe` to decrypt in memory, then patches the resolution and camera code |
| `hud.c` | HUD/menu correction (included by `sw2ws.c`) |
| `SW2Widescreen.ini` | Settings (resolution, HUD fix, background audio) |
| `version.rc` | Version information (name, author, version) |
| `build.bat` / `build.sh` | Build scripts |

Built with the standard MinGW C runtime; Windows APIs used: `kernel32.dll`, `user32.dll` (and `d3d9.dll`, `dsound.dll`, loaded at runtime).

## Build

32-bit Windows DLL, renamed `.asi`. Needs [Zig](https://ziglang.org) (tested) or MinGW-w64 32-bit (e.g. MSYS2 "MINGW32" shell).
Run `build.bat` (Windows) or `./build.sh` (Linux / macOS). Zig command used for the release:

```
zig cc -target x86-windows-gnu -O2 -s -shared -o SW2Widescreen.asi sw2ws.c version.rc -lkernel32 -luser32 -Wl,--subsystem,windows
```

## Changelog

- 2.0: compatible with HD Textures 2.0.3 (the game's own CreateDevice call is redirected, IDirect3D9 is never hooked);
  health bars and names in place; menu screens adapted (side pieces to the screen edges, full-screen backgrounds kept);
  fixed timer on the battle select screen, cut Select map, scrolling EXP background, stretched story screens;
  background audio after Alt+Tab (`DSBCAPS_GLOBALFOCUS`). `Debug=1` in the ini enables F7/F8/F9 test keys.

- 1.0.1: same features; built with the standard C runtime and version information (fewer antivirus false positives).
- 1.0: first release.

## Install

Copy `dinput8.dll` (Ultimate ASI Loader), `SW2Widescreen.asi` and `SW2Widescreen.ini` next to `sw2.exe`.
Delete them to uninstall.

## Credits

- **santerus349** – found the game's resolution functions ([SUWSF-Patches](https://github.com/santerus349/SUWSF-Patches))
- **ThirteenAG** – [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (MIT)
- **PhantomGamers** – [SUWSF](https://github.com/PhantomGamers/SUWSF), used as a starting point

Plugin: idiz117. Samurai Warriors 2 © Koei Tecmo / Omega Force. Unofficial fan project, not affiliated.

---

*Français : code source du mod 16:9 publié sur Nexus Mods. Le plugin corrige le jeu uniquement en mémoire au lancement
(résolution, caméra, HUD) ; aucun fichier du jeu n'est modifié. Compilation : voir « Build » ci-dessus.*
