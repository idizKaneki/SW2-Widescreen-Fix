/* SW2Widescreen.asi - Samurai Warriors 2 (PC) : vraie resolution large + camera Hor+
 * Charge par Ultimate ASI Loader (dinput8.dll).
 * - attend que sw2.exe (protege) se dechiffre en memoire, puis patche aussitot
 * - toutes les resolutions du menu deviennent la resolution choisie (auto = bureau)
 * - corrige le ratio de la camera 3D (plus d'etirement, champ de vision elargi)
 * - recentre le HUD et les menus 2D sans etirement (hud.c)
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef unsigned char u8; typedef unsigned int u32;
static HANDLE g_self;
static char g_ini[260], g_logp[260];
static char g_log[4096]; static int g_loglen;
static float g_aspect;               /* lu par le code patche du jeu */

#include "hud.c"

static void flushlog(void);
static void lg(const char *s) { while (*s && g_loglen < (int)sizeof g_log - 1) g_log[g_loglen++] = *s++; if (g_loglen && g_log[g_loglen-1] == '\n') flushlog(); }
static void lgn(int v) { char b[12]; int i = 11; b[i] = 0; if (v == 0) b[--i] = '0'; while (v > 0) { b[--i] = '0' + v % 10; v /= 10; } lg(b + i); }
static void lghex(u32 v) { char b[11]; int i; b[0]='0'; b[1]='x'; for (i = 0; i < 8; i++) b[2+i] = "0123456789ABCDEF"[(v >> (28 - 4*i)) & 15]; b[10]=0; lg(b); }
static void flushlog(void) {
    DWORD w; HANDLE h = CreateFileA(g_logp, 0x40000000, 1, 0, 2, 0x80, 0);
    if (h != (HANDLE)-1) { WriteFile(h, g_log, g_loglen, &w, 0); CloseHandle(h); }
}

/* recherche d'un motif (0x100 = joker) */
static u8 *find(u8 *p, u32 n, const short *pat, int m) {
    u32 i; int j;
    for (i = 0; i + m <= n; i++) {
        for (j = 0; j < m; j++) if (pat[j] != 0x100 && p[i+j] != (u8)pat[j]) break;
        if (j == m) return p + i;
    }
    return 0;
}
static void put(u8 *dst, const u8 *src, int n) {
    DWORD old; int i;
    VirtualProtect(dst, n, 0x40, &old);
    for (i = 0; i < n; i++) dst[i] = src[i];
    VirtualProtect(dst, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), dst, n);
}

static const short P_W[]  = {0xB8,0x00,0x04,0x00,0x00,0xC3,0x83};               /* mov eax,1024 ; ret */
static const short P_H[]  = {0xB8,0x00,0x03,0x00,0x00,0xC3,0x83};               /* mov eax,768  ; ret */
static const short P_FW[] = {0x0F,0xB6,0x05,0x100,0x100,0x100,0x100,0x85,0xC0,0x75,0x06,0xB8,0x80,0x02,0x00,0x00,0xC3}; /* largeur(index), 640 en 1er */
static const short P_FH[] = {0x0F,0xB6,0x05,0x100,0x100,0x100,0x100,0x85,0xC0,0x75,0x06,0xB8,0xE0,0x01,0x00,0x00,0xC3}; /* hauteur(index), 480 en 1er */
static const short P_AR[] = {0xD9,0x01,0xD8,0x71,0x04,0xEB,0x06,0xD9,0x05};     /* fld [ecx]; fdiv [ecx+4] (ratio camera) */

/* force la fonction entiere a renvoyer v, quel que soit le reglage choisi dans le jeu */
static void forcefunc(u8 *f, int v) { u8 c[6]; c[0] = 0xB8; c[1] = (u8)v; c[2] = (u8)(v>>8); c[3] = (u8)(v>>16); c[4] = (u8)(v>>24); c[5] = 0xC3; put(f, c, 6); }

static DWORD WINAPI worker(LPVOID arg) {
    u8 *base = (u8*)GetModuleHandleA(0);
    u32 pe = *(u32*)(base + 0x3C);
    u8 *sec = base + pe + 24 + *(unsigned short*)(base + pe + 20);
    u8 *text = base + *(u32*)(sec + 12); u32 tsz = *(u32*)(sec + 8);
    int w = (int)GetPrivateProfileIntA("Settings", "Width", 0, g_ini);
    int h = (int)GetPrivateProfileIntA("Settings", "Height", 0, g_ini);
    int maxh = (int)GetPrivateProfileIntA("Settings", "MaxHeight", 1440, g_ini);
    int fixcam = (int)GetPrivateProfileIntA("Settings", "FixCamera", 1, g_ini);
    int t; u8 *pw = 0, *ph = 0, *par = 0;
    (void)arg;
    lg("SW2Widescreen 1.0.1\r\n");
    if (w <= 0 || h <= 0) {
        w = GetSystemMetrics(0); h = GetSystemMetrics(1);
        lg("Ecran detecte : "); lgn(w); lg("x"); lgn(h); lg("\r\n");
        while (maxh > 0 && h > maxh && (w % 2) == 0 && (h % 2) == 0) { w /= 2; h /= 2; }
    }
    g_aspect = (float)w / (float)h;
    lg("Resolution utilisee : "); lgn(w); lg("x"); lgn(h); lg("\r\n");
    if (GetPrivateProfileIntA("Settings", "FixHUD", 1, g_ini)) {
        if (hud_install(w, h)) lg("Correction HUD/menus : active\r\n"); else lg("Correction HUD/menus : non necessaire ou impossible\r\n");
    } else lg("Correction HUD/menus desactivee\r\n");

    {
        /* surveillance continue : le jeu cree son affichage tres vite apres le dechiffrement */
        u32 t0 = GetTickCount(), tf = 0, it = 0;
        u8 *kw = (u8*)0x4CE180, *kh = (u8*)0x4CE1D0;   /* emplacements connus (sw2.exe US/EU) */
        int known = (u8*)text <= kw && kw + 0x60 <= (u8*)text + tsz;
        SetThreadPriority(GetCurrentThread(), 15);       /* THREAD_PRIORITY_TIME_CRITICAL */
        for (;;) {
            if (!pw) {
                if (known && kw[0] == 0x0F && kw[1] == 0xB6 && kw[2] == 0x05 && kw[11] == 0xB8 && kw[12] == 0x80 && kw[13] == 0x02)
                    pw = kw;
                else if ((it & 255) == 0) pw = find(text, tsz, P_FW, 17);
                if (pw) { forcefunc(pw, w); tf = GetTickCount();
                          lg("Largeur OK a "); lghex((u32)pw); lg(" ("); lgn((int)(tf - t0)); lg(" ms)\r\n"); }
            }
            if (pw && !ph) {
                if (known && kh[0] == 0x0F && kh[1] == 0xB6 && kh[2] == 0x05 && kh[11] == 0xB8 && kh[12] == 0xE0 && kh[13] == 0x01)
                    ph = kh;
                else ph = find(text, tsz, P_FH, 17);
                if (ph) { forcefunc(ph, h); lg("Hauteur OK a "); lghex((u32)ph); lg("\r\n"); }
            }
            if (pw && ph && !par && fixcam) par = find(text, tsz, P_AR, 9);
            if (pw && ph && (par || !fixcam)) break;
            if (tf && GetTickCount() - tf > 3000) break;
            if (GetTickCount() - t0 > 60000) break;
            it++;
            Sleep(0);
        }
        SetThreadPriority(GetCurrentThread(), 0);
    }
    if (!pw) lg("ERREUR : fonction largeur introuvable\r\n");
    if (!ph) lg("ERREUR : fonction hauteur introuvable\r\n");
    if (fixcam) {
        if (par) {
            u8 code[7]; u32 a = (u32)&g_aspect;
            code[0] = 0xD9; code[1] = 0x05; code[2] = (u8)a; code[3] = (u8)(a>>8); code[4] = (u8)(a>>16); code[5] = (u8)(a>>24); code[6] = 0x90;
            put(par, code, 7);
            lg("Camera OK a "); lghex((u32)par); lg("\r\n");
        } else lg("ERREUR : camera introuvable\r\n");
    } else lg("Correction camera desactivee\r\n");
    flushlog();
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID res) {
    (void)res;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        int i, n;
        g_self = inst;
        n = (int)GetModuleFileNameA(inst, g_ini, 250);
        for (i = n; i > 0 && g_ini[i-1] != '\\' && g_ini[i-1] != '/'; i--) ;
        { const char *a = "SW2Widescreen.ini", *b = "SW2Widescreen.log"; int k;
          for (k = 0; k < i; k++) g_logp[k] = g_ini[k];
          for (k = 0; a[k]; k++) g_ini[i+k] = a[k]; g_ini[i+k] = 0;
          for (k = 0; b[k]; k++) g_logp[i+k] = b[k]; g_logp[i+k] = 0;
          { const char *c = "SW2Widescreen_hud"; int q; for (q = 0; q < i; q++) h_dpath[q] = g_ini[q]; for (k = 0; c[k]; k++) h_dpath[i+k] = c[k]; h_dpath[i+k] = 0; } }
        CreateThread(0, 0, worker, 0, 0, 0);
    }
    return 1;
}
