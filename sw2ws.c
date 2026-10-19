/* SW2Widescreen.asi - Samurai Warriors 2 (PC): real widescreen resolution + Hor+ camera
 * Loaded by Ultimate ASI Loader (dinput8.dll).
 * - waits for the protected sw2.exe to decrypt itself in memory, then patches right away
 * - every resolution of the game's menu becomes the chosen one (auto = screen)
 * - fixes the 3D camera aspect ratio (no stretching, wider field of view)
 * - re-centres the 2D HUD and menus without stretching (hud.c)
 * 2.0: compatible with HD Textures 2.0.3 (the game's own CreateDevice call is redirected,
 *        IDirect3D9 is never hooked, so the two mods no longer loop into each other);
 *        auto resolution read in real pixels (Windows scaling ignored);
 *        HUD follows the image really created by the game; health bars and names in place;
 *        4:3 menu screens: backgrounds kept full screen, side pieces sent to the screen edges;
 *        off-screen elements left alone (chrono), scissor rectangles squeezed (Select map);
 *        camera patch off by default: the patched function is a secondary camera;
 *        background audio: sound keeps playing after Alt+Tab (DSBCAPS_GLOBALFOCUS).
 * Debug=1 in the ini: F7 side pieces on/off, F8 secondary camera mode, F9 capture of one frame.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef unsigned char u8; typedef unsigned int u32;
static char g_ini[260], g_logp[260];
static char g_log[8192]; static int g_loglen;
static float g_aspect;               /* read by the patched game code */

#include "hud.c"

static void flushlog(void) {
    DWORD w; HANDLE h;
    if (!g_loglen) return;
    h = CreateFileA(g_logp, FILE_APPEND_DATA, FILE_SHARE_READ, 0, OPEN_ALWAYS, 0x80, 0);
    if (h != INVALID_HANDLE_VALUE) { WriteFile(h, g_log, (DWORD)g_loglen, &w, 0); CloseHandle(h); }
    g_loglen = 0;
}
static CRITICAL_SECTION g_lcs;
static void lg(const char *s) {
    EnterCriticalSection(&g_lcs);
    while (*s) { if (g_loglen >= (int)sizeof g_log - 1) flushlog(); g_log[g_loglen++] = *s++; }
    if (g_loglen && g_log[g_loglen-1] == '\n') flushlog();
    LeaveCriticalSection(&g_lcs);
}
static void lgn(int v) { char b[12]; int i = 11, neg = v < 0; if (neg) v = -v; b[i] = 0; if (v == 0) b[--i] = '0'; while (v > 0) { b[--i] = '0' + v % 10; v /= 10; } if (neg) b[--i] = '-'; lg(b + i); }
static void lghex(u32 v) { char b[11]; int i; b[0]='0'; b[1]='x'; for (i = 0; i < 8; i++) b[2+i] = "0123456789ABCDEF"[(v >> (28 - 4*i)) & 15]; b[10]=0; lg(b); }
static void lgf(float f) { int neg = f < 0; u32 ip, fr; if (neg) { lg("-"); f = -f; } if (f > 1e9f || f != f) { lg("?"); return; }
    ip = (u32)f; fr = (u32)((f - (float)ip) * 10000.0f + 0.5f); if (fr >= 10000) { ip++; fr -= 10000; }
    lgn((int)ip); lg("."); if (fr < 1000) lg("0"); if (fr < 100) lg("0"); if (fr < 10) lg("0"); lgn((int)fr); }

/* pattern search (0x100 = wildcard) */
static u8 *find(u8 *p, u32 n, const short *pat, int m) {
    u32 i; int j;
    for (i = 0; i + m <= n; i++) {
        for (j = 0; j < m; j++) if (pat[j] != 0x100 && p[i+j] != (u8)pat[j]) break;
        if (j == m) return p + i;
    }
    return 0;
}
static int same(const u8 *p, const short *pat, int m) { int j; for (j = 0; j < m; j++) if (pat[j] != 0x100 && p[j] != (u8)pat[j]) return 0; return 1; }
static void put(u8 *dst, const u8 *src, int n) {
    DWORD old; int i;
    VirtualProtect(dst, n, 0x40, &old);
    for (i = 0; i < n; i++) dst[i] = src[i];
    VirtualProtect(dst, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
}

static const short P_FW[] = {0x0F,0xB6,0x05,0x100,0x100,0x100,0x100,0x85,0xC0,0x75,0x06,0xB8,0x80,0x02,0x00,0x00,0xC3}; /* width(index), 640 first */
static const short P_FH[] = {0x0F,0xB6,0x05,0x100,0x100,0x100,0x100,0x85,0xC0,0x75,0x06,0xB8,0xE0,0x01,0x00,0x00,0xC3}; /* height(index), 480 first */
static const short P_CD[] = {0x8B,0x85,0xE8,0x00,0x00,0x00,0x50,0xFF,0x52,0x40,0x85,0xC0,0x0F,0x85}; /* mov eax,[ebp+E8]; push eax; call [edx+40] (CreateDevice) */
static const short P_AR[] = {0xD9,0x01,0xD8,0x71,0x04,0xEB,0x06,0xD9,0x05};     /* fld [ecx]; fdiv [ecx+4]; jmp +6 (camera ratio) */

/* forces the whole function to return v, whatever is chosen in the game's options */
static void forcefunc(u8 *f, int v) { u8 c[6]; c[0] = 0xB8; c[1] = (u8)v; c[2] = (u8)(v>>8); c[3] = (u8)(v>>16); c[4] = (u8)(v>>24); c[5] = 0xC3; put(f, c, 6); }

/* game's CreateDevice call -> jmp cave: mov eax,[ebp+E8]; push eax; call ws_cd; jmp back */
static int redirect_cd(u8 *p) {
    u8 *c = (u8*)VirtualAlloc(0, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); u8 j[10]; int i;
    if (!c) return 0;
    for (i = 0; i < 7; i++) c[i] = p[i];                                   /* mov eax,[ebp+E8]; push eax */
    c[7] = 0xE8; *(int*)(c + 8) = (int)((u32)ws_cd - ((u32)c + 12));     /* call ws_cd */
    c[12] = 0xE9; *(int*)(c + 13) = (int)(((u32)p + 10) - ((u32)c + 17)); /* jmp back after the call */
    j[0] = 0xE9; *(int*)(j + 1) = (int)((u32)c - ((u32)p + 5));
    for (i = 5; i < 10; i++) j[i] = 0x90;
    put(p, j, 10);
    return 1;
}

/* ---------- camera ----------
 * Game code (projection matrix, ecx = {tanX, tanY, near, far}):
 *   ratio = tanX / tanY ; jmp skip ; (if tanY == 0: ratio = 1.0) ; skip: ...
 * The patch jumps to a small code block (one per mode) that stores ecx (for the log)
 * and loads the ratio. Changing mode = changing the target of that single jump.
 *   mode 1 (new, 1.0.2): ratio = screen ratio, then continue after the default value
 *   mode 2 (1.0.1)     : ratio = screen ratio, then ALSO the default value (old behaviour)
 *   mode 3 (off)       : the game's own ratio */
static u8 *g_par; static u8 *g_cave[4]; static volatile u32 g_camp; static int g_cammode;
static const char *g_camname[4] = { "", "new (1.0.2)", "old (1.0.1)", "off (game)" };
static u8 *mkcave(int mode) {
    u8 *c = (u8*)VirtualAlloc(0, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE); u32 a; u8 *tgt;
    if (!c) return 0;
    a = (u32)&g_camp; c[0] = 0x89; c[1] = 0x0D; *(u32*)(c + 2) = a;                       /* mov [g_camp], ecx */
    if (mode == 3) { c[6] = 0xD9; c[7] = 0x01; c[8] = 0xD8; c[9] = 0x71; c[10] = 0x04; c[11] = 0x90; }   /* fld [ecx]; fdiv [ecx+4] */
    else { a = (u32)&g_aspect; c[6] = 0xD9; c[7] = 0x05; *(u32*)(c + 8) = a; }             /* fld [g_aspect] */
    tgt = g_par + (mode == 2 ? 7 : 13);
    c[12] = 0xE9; *(int*)(c + 13) = (int)((u32)tgt - ((u32)c + 17));
    return c;
}
static void cam_set(int mode) {
    u8 j[7];
    if (!g_par || mode < 1 || mode > 3 || !g_cave[mode]) return;
    j[0] = 0xE9; *(int*)(j + 1) = (int)((u32)g_cave[mode] - ((u32)g_par + 5)); j[5] = 0x90; j[6] = 0x90;
    if (g_cammode) put(g_par + 1, j + 1, 4);        /* already patched: only the jump target changes */
    else put(g_par, j, 7);
    g_cammode = mode;
}
static int cam_read(float *e) {
    SIZE_T n = 0; u32 p = g_camp;
    return p && ReadProcessMemory(GetCurrentProcess(), (void*)p, e, 16, &n) && n == 16;
}
static void cam_log(void) {
    float e[4];
    lg("Camera mode: "); lg(g_camname[g_cammode]); lg("\r\n");
    if (cam_read(e) && e[1] != 0.0f && e[2] != 0.0f) {
        float A = 2.0f * e[1] * e[2], r = e[0] / e[1];
        lg("  tanX "); lgf(e[0]); lg(" tanY "); lgf(e[1]); lg(" near "); lgf(e[2]); lg(" far "); lgf(e[3]); lg(" game ratio "); lgf(r); lg("\r\n");
        lg("  scale X/Y  game: "); lgf(2*e[2]/(r*A)); lg(" / "); lgf(2*e[2]/A);
        lg("  new: "); lgf(2*e[2]/(g_aspect*A)); lg(" / "); lgf(2*e[2]/A);
        lg("  1.0.1: "); lgf(2*e[2]/g_aspect); lg(" / "); lgf(2*e[2]/g_aspect); lg("\r\n");
    }
}
static void ws_capcam(void) {
    float e[4];
    cl("camera mode "); cl(g_camname[g_cammode]); cl(" | menu sides "); cl(h_edges ? "on" : "off");
    if (cam_read(e)) { cl(" | tanX "); clf(e[0]); cl(" tanY "); clf(e[1]); cl(" near "); clf(e[2]); cl(" far "); clf(e[3]); }
    cl("\r\n");
}

/* ---------- debug hotkeys (Debug=1 in the ini), once per frame ----------
 * F7 menu pieces against the sides / centred, F8 secondary camera mode, F9 capture of one frame's 2D quads */
static int g_k7, g_k8, g_k9, g_debug;
static int key(int vk, int *prev) { int d = (GetAsyncKeyState(vk) & 0x8000) != 0, hit = d && !*prev; *prev = d; return hit; }
static volatile LONG a_n; static u32 g_nfr;
static void ws_frame(void) {
    HWND fw; DWORD pid = 0;
    if (++g_nfr == 1800) { lg("Sound buffers set to background audio: "); lgn((int)a_n); lg("\r\n"); }
    if (!g_debug) return;
    fw = GetForegroundWindow();
    if (!fw) return;
    GetWindowThreadProcessId(fw, &pid);
    if (pid != GetCurrentProcessId()) { g_k7 = g_k8 = g_k9 = 0; return; }
    if (key(VK_F8, &g_k8) && g_par) { cam_set(g_cammode == 2 ? 1 : g_cammode == 1 ? 3 : 2); cam_log(); }   /* 1.0.1 -> new -> off */
    if (key(VK_F7, &g_k7)) { h_edges = !h_edges; lg(h_edges ? "F7: menu pieces against the sides\r\n" : "F7: menu pieces centred\r\n"); }
    if (key(VK_F9, &g_k9) && !h_cap) h_cap = 1;
}

/* real resolution of the main screen, in physical pixels.
   GetSystemMetrics returns a smaller size when Windows applies scaling (125 %, 150 %...)
   and the game is not "DPI aware": EnumDisplaySettings gives the real display mode. */
static void screen(int *w, int *h) {
    DEVMODEA dm; ZeroMemory(&dm, sizeof dm); dm.dmSize = sizeof dm;
    if (EnumDisplaySettingsA(0, ENUM_CURRENT_SETTINGS, &dm) && dm.dmPelsWidth && dm.dmPelsHeight) {
        *w = (int)dm.dmPelsWidth; *h = (int)dm.dmPelsHeight;
    } else { *w = GetSystemMetrics(SM_CXSCREEN); *h = GetSystemMetrics(SM_CYSCREEN); }
}


/* Background audio (BackgroundAudio=1): the game's sound buffers are created without
 * DSBCAPS_GLOBALFOCUS, so DirectSound mutes them as soon as the game loses focus (Alt+Tab):
 * dialogue that plays meanwhile (e.g. during a loading screen) is lost.
 * We add GLOBALFOCUS to every secondary buffer. The IDirectSound(8) vtables live in dsound.dll
 * and are shared by every object, so a temporary object is enough to find them. */
typedef int (WINAPI *DSCreateF)(const void*, void**, void*);
typedef int (WINAPI *CSBF)(void*, u32*, void**, void*);
static CSBF oCSB7, oCSB8;
static int csb(CSBF o, void *ds, u32 *desc, void **out, void *u) {
    if (desc && desc[0] >= 20 && !(desc[1] & 1)) {           /* DSBUFFERDESC: dwSize, dwFlags; not the primary buffer */
        desc[1] = (desc[1] & ~0x4000u) | 0x8000u;              /* -STICKYFOCUS, +GLOBALFOCUS */
        InterlockedIncrement(&a_n);
    }
    return o(ds, desc, out, u);
}
static int WINAPI hCSB7(void *ds, u32 *d, void **o, void *u) { return csb(oCSB7, ds, d, o, u); }
static int WINAPI hCSB8(void *ds, u32 *d, void **o, void *u) { return csb(oCSB8, ds, d, o, u); }
static DWORD WINAPI audio_thread(LPVOID arg) {
    HMODULE m = LoadLibraryA("dsound.dll"); void *ds = 0; int ok = 0;
    DSCreateF c7 = m ? (DSCreateF)GetProcAddress(m, "DirectSoundCreate") : 0;
    DSCreateF c8 = m ? (DSCreateF)GetProcAddress(m, "DirectSoundCreate8") : 0;
    (void)arg;
    if (c8 && c8(0, &ds, 0) == 0 && ds) { void **vt = *(void***)ds; if (vt[3] != (void*)hCSB7) { vhook(vt, 3, (void*)hCSB8, (void**)&oCSB8); ok++; } ((u32 (WINAPI*)(void*))vt[2])(ds); ds = 0; }
    if (c7 && c7(0, &ds, 0) == 0 && ds) { void **vt = *(void***)ds; if (vt[3] != (void*)hCSB8) { vhook(vt, 3, (void*)hCSB7, (void**)&oCSB7); ok++; } ((u32 (WINAPI*)(void*))vt[2])(ds); }
    lg(ok ? "Background audio: on\r\n" : "Background audio: impossible (dsound)\r\n");
    return 0;
}

static DWORD WINAPI worker(LPVOID arg) {
    u8 *base = (u8*)GetModuleHandleA(0);
    u32 pe = *(u32*)(base + 0x3C);
    u8 *sec = base + pe + 24 + *(unsigned short*)(base + pe + 20);
    u8 *text = base + *(u32*)(sec + 12); u32 tsz = *(u32*)(sec + 8);
    int w = (int)GetPrivateProfileIntA("Settings", "Width", 0, g_ini);
    int h = (int)GetPrivateProfileIntA("Settings", "Height", 0, g_ini);
    int maxh = (int)GetPrivateProfileIntA("Settings", "MaxHeight", 1440, g_ini);
    int fixcam = (int)GetPrivateProfileIntA("Settings", "FixCamera", 1, g_ini);
    u8 *pw = 0, *ph = 0, *par = 0, *pc = 0;
    (void)arg;
    lg("SW2Widescreen 2.0\r\n");
    if (GetModuleHandleA("SW2HD.asi")) lg("HD Textures module present\r\n");
    if (w <= 0 || h <= 0) {
        screen(&w, &h);
        lg("Screen detected: "); lgn(w); lg("x"); lgn(h); lg("\r\n");
        while (maxh > 0 && h > maxh && (w % 2) == 0 && (h % 2) == 0) { w /= 2; h /= 2; }
    }
    g_aspect = (float)w / (float)h;
    lg("Resolution used: "); lgn(w); lg("x"); lgn(h); lg("\r\n");
    h_fix = (int)GetPrivateProfileIntA("Settings", "FixHUD", 1, g_ini);
    h_borderless = (int)GetPrivateProfileIntA("Settings", "Borderless", 0, g_ini);   /* experimental, undocumented: breaks sound and exit */
    g_debug = (int)GetPrivateProfileIntA("Settings", "Debug", 0, g_ini);
    if (hud_install(w, h)) lg(h_fix ? "HUD/menu fix: on\r\n" : "HUD/menu fix: off\r\n");
    else lg("HUD/menu fix: impossible (d3d9)\r\n");

    {
        /* continuous watch: the game creates its display very soon after decrypting */
        u32 t0 = GetTickCount(), tf = 0, it = 0;
        u8 *kw = (u8*)0x4CE180, *kh = (u8*)0x4CE1D0;   /* known places (sw2.exe US/EU) */
        int known = (u8*)text <= kw && kw + 0x60 <= (u8*)text + tsz;
        SetThreadPriority(GetCurrentThread(), 15);       /* THREAD_PRIORITY_TIME_CRITICAL */
        for (;;) {
            if (!pw) {
                if (known && kw[0] == 0x0F && kw[1] == 0xB6 && kw[2] == 0x05 && kw[11] == 0xB8 && kw[12] == 0x80 && kw[13] == 0x02)
                    pw = kw;
                else if ((it & 255) == 0) pw = find(text, tsz, P_FW, 17);
                if (pw) { forcefunc(pw, w); tf = GetTickCount();
                          lg("Width OK at "); lghex((u32)pw); lg(" ("); lgn((int)(tf - t0)); lg(" ms)\r\n"); }
            }
            if (pw && !ph) {
                if (known && kh[0] == 0x0F && kh[1] == 0xB6 && kh[2] == 0x05 && kh[11] == 0xB8 && kh[12] == 0xE0 && kh[13] == 0x01)
                    ph = kh;
                else ph = find(text, tsz, P_FH, 17);
                if (ph) { forcefunc(ph, h); lg("Height OK at "); lghex((u32)ph); lg("\r\n"); }
            }
            if (!pc && (h_fix || h_borderless)) {                          /* the game's CreateDevice call: decrypted a bit later */
                u8 *kc = (u8*)0x5FBE92;
                if (known && same(kc, P_CD, 14)) pc = kc;
                else if ((it & 1023) == 0) pc = find(text, tsz, P_CD, 14);
                if (pc) { if (redirect_cd(pc)) { lg("HUD: CreateDevice redirected at "); lghex((u32)pc); lg(" ("); lgn((int)(GetTickCount() - t0)); lg(" ms)\r\n"); } }
            }
            if (pw && ph && !par) par = find(text, tsz, P_AR, 9);
            if (pw && ph && par && (pc || !(h_fix || h_borderless))) break;
            if (tf && GetTickCount() - tf > 15000) break;
            if (GetTickCount() - t0 > 60000) break;
            it++;
            Sleep(0);
        }
        SetThreadPriority(GetCurrentThread(), 0);
    }
    if (!pw) lg("ERROR: width function not found\r\n");
    if (!ph) lg("ERROR: height function not found\r\n");
    if ((h_fix || h_borderless) && !pc) lg("ERROR: CreateDevice call not found (HUD fix and borderless window inactive)\r\n");
    if (par) {
        g_par = par;
        g_cave[1] = mkcave(1); g_cave[2] = mkcave(2); g_cave[3] = mkcave(3);
        cam_set(fixcam == 2 ? 2 : fixcam == 3 ? 1 : 3);   /* this function is not the main camera: game code by default */
        lg("Camera OK at "); lghex((u32)par); lg(" - mode "); lg(g_camname[g_cammode]); lg("\r\n");
    } else lg("ERROR: camera not found\r\n");
    flushlog();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res) {
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        int i, n, k;
        DisableThreadLibraryCalls(inst);
        n = (int)GetModuleFileNameA(inst, g_ini, 250);
        for (i = n; i > 0 && g_ini[i-1] != '\\' && g_ini[i-1] != '/'; i--) {}
        { const char *a = "SW2Widescreen.ini", *b = "SW2Widescreen.log", *c = "SW2Widescreen_capture.txt";
          for (k = 0; k < i; k++) { g_logp[k] = g_ini[k]; h_cpath[k] = g_ini[k]; }
          for (k = 0; a[k]; k++) g_ini[i+k] = a[k]; g_ini[i+k] = 0;
          for (k = 0; b[k]; k++) g_logp[i+k] = b[k]; g_logp[i+k] = 0;
          for (k = 0; c[k]; k++) h_cpath[i+k] = c[k]; h_cpath[i+k] = 0; }
        DeleteFileA(g_logp);
        InitializeCriticalSection(&g_lcs);
        CreateThread(0, 0, worker, 0, 0, 0);
        if (GetPrivateProfileIntA("Settings", "BackgroundAudio", 1, g_ini)) CreateThread(0, 0, audio_thread, 0, 0, 0);
    }
    return 1;
}
