/* hud.c - correction du HUD / des menus (elements 2D en coordonnees d'ecran, format POSITIONT)
 * Le jeu place ses elements 2D comme si l'ecran 4:3 etait etire sur toute la largeur.
 * Juste avant chaque dessin 2D, on resserre les X vers le centre (facteur (4/3)/ratio) :
 * les elements retrouvent leurs proportions et restent groupes au centre.
 * Exceptions : quads couvrant toute la largeur (fonds, fondus, bandeaux) et rendus hors ecran. */
__declspec(dllimport) HANDLE WINAPI LoadLibraryA(const char*);
__declspec(dllimport) void* WINAPI GetProcAddress(HANDLE, const char*);

#define VTBL(o) (*(void***)(o))
static float h_W, h_H, h_s, h_cx;           /* ecran, facteur, centre */
static int h_isT, h_posoff;                 /* declaration courante : POSITIONT ? */
static void *h_vb; static u32 h_vboff, h_stride;
static u32 h_vp[4];
static int h_count;
static u32 h_az[256]; static float h_ax[256]; static int h_na;
__declspec(dllimport) short WINAPI GetAsyncKeyState(int);
static char h_dpath[270]; static char *h_dbuf; static int h_dlen, h_dcap, h_dprev, h_ndump, h_draw;
static char h_dmem[4 << 20];
static void dl(const char *s) { while (*s && h_dlen < (int)sizeof h_dmem - 64) h_dmem[h_dlen++] = *s++; }
static void dlu(u32 v) { char b[12]; int i = 11; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = '0' + v % 10; v /= 10; } dl(b + i); }
static void dlf(float f) {
    u32 bits = *(u32*)&f;
    if ((bits & 0x7F800000) == 0x7F800000) { dl("nan"); return; }
    if (f < 0) { dl("-"); f = -f; }
    if (f > 2000000000.0f) { dl("big"); return; }
    { u32 ip = (u32)f; u32 fr = (u32)((f - (float)ip) * 100000.0f + 0.5f); if (fr >= 100000) { ip++; fr -= 100000; }
      dlu(ip); dl("."); if (fr < 10000) dl("0"); if (fr < 1000) dl("0"); if (fr < 100) dl("0"); if (fr < 10) dl("0"); dlu(fr); }
}
static void lg(const char *s);
static int h_seen[8];
#define ONCE(i,msg) do { if (!h_seen[i]) { h_seen[i] = 1; lg(msg); } } while (0)                         /* nb de lots corriges (log) */

typedef struct { void *vb; u32 a, b; } Rng;
static Rng h_done[1024]; static int h_ndone, h_head;

typedef int (WINAPI *SetDeclF)(void*, void*);
typedef int (WINAPI *DPF)(void*, u32, u32, u32);
typedef int (WINAPI *DIPF)(void*, u32, int, u32, u32, u32, u32);
typedef int (WINAPI *SSSF)(void*, u32, void*, u32, u32);
typedef int (WINAPI *SetVpF)(void*, const u32*);
typedef int (WINAPI *LockF)(void*, u32, u32, void**, u32);
typedef int (WINAPI *UnlockF)(void*);
typedef int (WINAPI *CDF)(void*, u32, u32, HANDLE, u32, u32*, void**);
typedef int (WINAPI *PresentF)(void*, void*, void*, void*, void*);
static PresentF oPresent;
static int WINAPI hPresent(void *d, void *a, void *b, void *c, void *e) {
    h_na = 0;                                                /* nouvelles ancres a chaque image */
    return oPresent(d, a, b, c, e);
}
static SetDeclF oSetDecl; static DPF oDP; static DIPF oDIP; static SSSF oSSS; static SetVpF oSetVp;
static CDF oCD;
static void **h_vts[8]; static LockF h_olock[8]; static int h_nvt;   /* un Lock d'origine par type de tampon */
static int h_self;                                                    /* nos propres Lock ne comptent pas */
static LockF origlock(void *vb) { int i; void **vt = VTBL(vb); for (i = 0; i < h_nvt; i++) if (h_vts[i] == vt) return h_olock[i]; return (LockF)vt[11]; }

static void vhook(void **vt, int idx, void *fn, void **orig) {
    u32 old; if (vt[idx] == fn) return;
    VirtualProtect(&vt[idx], 4, 0x40, &old); *orig = vt[idx]; vt[idx] = fn; VirtualProtect(&vt[idx], 4, old, &old);
}

/* le jeu reecrit une zone du tampon : on oublie qu'elle etait deja corrigee */
static int WINAPI hLock(void *vb, u32 off, u32 size, void **p, u32 fl) {
    int i;
    if (h_self) return origlock(vb)(vb, off, size, p, fl);
    ONCE(0, "HUD: Lock intercepte\r\n");
    for (i = 0; i < h_ndone; i++) {
        Rng *r = &h_done[i];
        if (r->vb == vb && (size == 0 || (off < r->b && r->a < off + size))) r->vb = 0;
    }
    return origlock(vb)(vb, off, size, p, fl);
}
static int already(void *vb, u32 a, u32 b) {
    int i;
    for (i = 0; i < h_ndone; i++) if (h_done[i].vb == vb && h_done[i].a <= a && b <= h_done[i].b) return 1;
    h_done[h_head].vb = vb; h_done[h_head].a = a; h_done[h_head].b = b;
    h_head = (h_head + 1) & 1023; if (h_ndone < 1024) h_ndone++;
    return 0;
}

/* chaque sommet corrige est marque : rhw passe de 1.0 (0x3F800000) a 0x3F800001 (invisible) */
/* elements accroches aux personnages (noms, barres de vie) : profondeur 0.01..0.99999.
   Leur position est deja juste (camera corrigee) : on les retrecit autour de leur propre centre.
   Barre de fond, remplissage et nom d'un meme personnage partagent la meme profondeur -> meme ancre. */
static int isworld(float z) { return z > 0.01f && z < 0.99999f; }
static float anchor(u8 *p, u32 n, u32 zb) {
    int k; u32 i; float mn = 1e9f, mx = -1e9f;
    for (k = 0; k < h_na; k++) if (h_az[k] == zb) return h_ax[k];
    for (i = 0; i < n; i++) { u8 *v = p + i * h_stride + h_posoff;
        if (*(u32*)(v + 8) == zb) { float x = *(float*)v; if (x < mn) mn = x; if (x > mx) mx = x; } }
    if (h_na < 256) { h_az[h_na] = zb; h_ax[h_na] = (mn + mx) * 0.5f; h_na++; }
    return (mn + mx) * 0.5f;
}
#define RHW_ORIG 0x3F800000u
#define RHW_MARK 0x3F800001u
static void squeeze(u32 first, u32 n) {
    u8 *p; u32 i, j;
    if (!h_isT || !h_vb || h_posoff < 0 || h_stride < 16 || n == 0) return;
    if (h_vp[0] != 0 || (float)h_vp[2] != h_W) return;          /* seulement le rendu plein ecran */
    ONCE(1, "HUD: premier lot 2D\r\n");
    if (h_dcap) { h_draw++; dl("DRAW "); dlu(h_draw); dl(" n"); dlu(n); dl("\r\n"); }
    if (((LockF)VTBL(h_vb)[11])(h_vb, h_vboff + first * h_stride, n * h_stride, (void**)&p, 0x1000) != 0) { ONCE(2, "HUD: Lock refuse\r\n"); return; }
    for (i = 0; i < n; i += 4) {
        u32 m = (n - i < 4) ? n - i : 4, todo = 0;
        float mn = 1e9f, mx = -1e9f;
        for (j = 0; j < m; j++) {
            u8 *v = p + (i + j) * h_stride + h_posoff; float x = *(float*)v;
            if (*(u32*)(v + 12) == RHW_ORIG) todo++;
            if (x < mn) mn = x; if (x > mx) mx = x;
        }
        if (h_dcap) {
            u8 *v0 = p + i * h_stride + h_posoff; float my0 = 1e9f, my1 = -1e9f;
            for (j = 0; j < m; j++) { float y = *(float*)(p + (i + j) * h_stride + h_posoff + 4); if (y < my0) my0 = y; if (y > my1) my1 = y; }
            dl("d"); dlu(h_draw); dl(" q"); dlu(i / 4); dl(" x "); dlf(mn); dl(" "); dlf(mx); dl(" y "); dlf(my0); dl(" "); dlf(my1);
            dl(" z "); dlf(*(float*)(v0 + 8)); dl(" rhw "); dlf(*(float*)(v0 + 12)); dl(todo ? "" : " (deja)"); dl("\r\n");
        }
        if (!todo) continue;                                         /* deja corrige (ou rhw particulier) */
        if (mn <= 0.0f && mx >= h_W - 2.0f) continue;              /* toute la largeur : fond, fondu, bandeau */
        for (j = 0; j < m; j++) {
            u8 *v = p + (i + j) * h_stride + h_posoff;
            float z = *(float*)(v + 8);
            if (*(u32*)(v + 12) != RHW_ORIG) continue;
            if (isworld(z)) { float ax = anchor(p, n, *(u32*)(v + 8)); *(float*)v = ax + (*(float*)v - ax) * h_s; }
            else *(float*)v = h_cx + (*(float*)v + 0.5f - h_cx) * h_s - 0.5f;
            *(u32*)(v + 12) = RHW_MARK;
        }
    }
    ((UnlockF)VTBL(h_vb)[12])(h_vb);
    h_count++;
    ONCE(3, "HUD: premier lot corrige\r\n");
}

static int WINAPI hSetDecl(void *d, void *decl) {
    h_isT = 0; h_posoff = -1;
    if (decl) {
        u8 el[8*64]; u32 n = 64; int i;
        typedef int (WINAPI *GDF)(void*, void*, u32*);
        if (((GDF)VTBL(decl)[4])(decl, el, &n) == 0)
            for (i = 0; i < (int)n && el[i*8] != 0xFF; i++)
                if (*(unsigned short*)&el[i*8] == 0 && el[i*8+6] == 9 && el[i*8+4] == 3) { h_isT = 1; h_posoff = *(unsigned short*)&el[i*8+2]; break; }
    }
    return oSetDecl(d, decl);
}
static int WINAPI hSSS(void *d, u32 s, void *vb, u32 off, u32 stride) {
    if (s == 0) {
        h_vb = vb; h_vboff = off; h_stride = stride;
    }
    return oSSS(d, s, vb, off, stride);
}
static int WINAPI hSetVp(void *d, const u32 *vp) { if (vp) { h_vp[0] = vp[0]; h_vp[1] = vp[1]; h_vp[2] = vp[2]; h_vp[3] = vp[3]; } return oSetVp(d, vp); }
static int WINAPI hDP(void *d, u32 t, u32 sv, u32 pc) {
    if (h_isT) { u32 n = (t == 4) ? pc * 3 : (t == 5 || t == 6) ? pc + 2 : 0; squeeze(sv, n); }
    return oDP(d, t, sv, pc);
}
static int WINAPI hDIP(void *d, u32 t, int bv, u32 mi, u32 nv, u32 si, u32 pc) {
    if (h_isT) squeeze((u32)(bv + (int)mi), nv);
    return oDIP(d, t, bv, mi, nv, si, pc);
}
static int WINAPI hCD(void *d3d, u32 ad, u32 ty, HANDLE w, u32 fl, u32 *pp, void **out) {
    int r;
    lg("HUD: CreateDevice appele\r\n");
    r = oCD(d3d, ad, ty, w, fl, pp, out);
    lg(r == 0 ? "HUD: CreateDevice OK\r\n" : "HUD: CreateDevice ECHEC\r\n");
    if (r == 0 && out && *out) {
        void **vt = VTBL(*out);
        vhook(vt, 17, (void*)hPresent, (void**)&oPresent);
        vhook(vt, 47, (void*)hSetVp, (void**)&oSetVp);
        vhook(vt, 81, (void*)hDP, (void**)&oDP);
        vhook(vt, 82, (void*)hDIP, (void**)&oDIP);
        vhook(vt, 87, (void*)hSetDecl, (void**)&oSetDecl);
        vhook(vt, 100, (void*)hSSS, (void**)&oSSS);
    }
    return r;
}
static int hud_install(int w, int h) {
    typedef void *(WINAPI *C9)(u32);
    HANDLE m = LoadLibraryA("d3d9.dll"); C9 c; void *d3d;
    h_W = (float)w; h_H = (float)h; h_cx = h_W * 0.5f;
    h_s = (4.0f / 3.0f) / (h_W / h_H);
    if (h_s >= 0.999f) return 0;                 /* ecran 4:3 ou plus etroit : rien a faire */
    lg("HUD: chargement d3d9\r\n");
    if (!m) return 0;
    c = (C9)GetProcAddress(m, "Direct3DCreate9"); if (!c) return 0;
    d3d = c(32); if (!d3d) return 0;
    vhook(VTBL(d3d), 16, (void*)hCD, (void**)&oCD);
    lg("HUD: d3d9 pret\r\n");
    return 1;
}
