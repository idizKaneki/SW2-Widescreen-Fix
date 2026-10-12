# Samurai Warriors 2 (PC) – Widescreen Fix

Source code of **SW2Widescreen.asi**, the plugin behind the Nexus Mods release
[Widescreen Fix 16:9 / 21:9](https://www.nexusmods.com/samuraiwarriors2/mods/5).

The PC port of Samurai Warriors 2 only offers 4:3 resolutions. This plugin, loaded at game start by
[Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (`dinput8.dll`, MIT license, by ThirteenAG),
patches the game **in memory only** (no game file is modified):

- replaces the resolution functions so the game runs at the desktop resolution (or the one set in `SW2Widescreen.ini`);
- fixes the 3D camera aspect ratio (Hor+, no stretching);
- re-centers the 2D HUD and menus with correct proportions (`hud.c`, Direct3D 9 hooks).

## Files

| File | Role |
|---|---|
| `sw2ws.c` | Plugin entry point: waits for the protected `sw2.exe` to decrypt in memory, then patches the resolution and camera code |
| `hud.c` | HUD/menu correction (included by `sw2ws.c`) |
| `SW2Widescreen.ini` | Settings (resolution, camera fix, HUD fix) |

The code uses no C runtime: only `kernel32.dll` and `user32.dll` (and `d3d9.dll`, loaded at runtime).

## Build

32-bit Windows DLL, renamed `.asi`.

**MinGW-w64 (e.g. MSYS2 "MINGW32" shell):**

```
i686-w64-mingw32-gcc -O2 -shared -nostdlib -fno-builtin -ffreestanding -o SW2Widescreen.asi sw2ws.c -Wl,-e,_DllMain@12 -lkernel32 -luser32
```

**LLVM / clang (tested):**

```
clang --target=i686-w64-windows-gnu -O2 -fno-builtin -ffreestanding -c sw2ws.c -o sw2ws.o
ld.lld -m i386pe --shared -e _DllMain@12 --subsystem windows -o SW2Widescreen.asi sw2ws.o -lkernel32 -luser32
```

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
