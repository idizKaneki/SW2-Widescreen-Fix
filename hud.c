/* hud.c - HUD / menu fix (2D elements drawn in screen coordinates, POSITIONT / XYZRHW).
 * The game places its 2D elements as if the 4:3 screen were stretched over the whole width.
 * Right before each 2D draw, X coordinates are pulled towards the centre (factor (4/3)/aspect):
 * elements get their proportions back and stay grouped in the centre 4:3 area.
 * Exceptions: quads covering the whole width (backgrounds, fades, banners) and off-screen renders.
 * 1.0.2: reference size = the game's real back buffer (CreateDevice / Reset);
 *        SetFVF and DrawPrimitiveUP / DrawIndexedPrimitiveUP handled;
 *        scissor rectangles follow the squeezed elements (Select map);
 *        menu pieces touching a corner are sent to that screen edge, full-screen backgrounds kept,
 *        off-screen elements and pause veils handled (see squeeze_mem);
 *        F9 debug capture of every 2D quad of one frame (SW2Widescreen_capture.txt, Debug=1). */

#define VTBL(o) (*(void***)(o))
static float h_W, h_H, h_s, h_cx;           /* screen, factor, centre */
static int h_isT, h_posoff;                 /* current vertex format: pre-transformed (XYZRHW)? */
static void *h_vb; static u32 h_vboff, h_stride;
static u32 h_vp[6];
static int h_fix = 1;                       /* FixHUD */
static int h_world = 1;                     /* names / health bars: squeeze around their own centre */
static u32 h_az[256]; static float h_ax[256]; static int h_na;
static int h_scis_on; static int h_scis[4]; static int h_has_scis; static void *h_dev_scis;
static void lg(const char *s);
static void lgn(int v);
static void ws_frame(void);                 /* sw2ws.c : hotkeys, once per frame */
static int h_seen[8];
#define ONCE(i,msg) do { if (!h_seen[i]) { h_seen[i] = 1; lg(msg); } } while (0)

/* ---------- F9 capture ---------- */
static char h_cpath[270];
static char *h_cbuf; static int h_clen; static int h_cap, h_capn, h_draw;
#define CAPMAX (6 << 20)
static void cl(const char *s) { if (!h_cbuf) return; while (*s && h_clen < CAPMAX - 64) h_cbuf[h_clen++] = *s++; }
static void clu(u32 v) { char b[12]; int i = 11; b[i] = 0; if (!v) b[--i] = '0'; while (v) { b[--i] = '0' + v % 10; v /= 10; } cl(b + i); }
static void cli(int v) { if (v < 0) { cl("-"); v = -v; } clu((u32)v); }
static void clx(u32 v) { char b[9]; int i; for (i = 0; i < 8; i++) b[i] = "0123456789ABCDEF"[(v >> (28 - 4*i)) & 15]; b[8] = 0; cl(b); }
static void clf(float f) {
    u32 bits = *(u32*)&f;
    if ((bits & 0x7F800000) == 0x7F800000) { cl("nan"); return; }
    if (f < 0) { cl("-"); f = -f; }
    if (f > 2000000000.0f) { cl("big"); return; }
    { u32 ip = (u32)f; u32 fr = (u32)((f - (float)ip) * 100000.0f + 0.5f); if (fr >= 100000) { ip++; fr -= 100000; }
      clu(ip); cl("."); if (fr < 10000) cl("0"); if (fr < 1000) cl("0"); if (fr < 100) cl("0"); if (fr < 10) cl("0"); clu(fr); }
}
static void capflush(void) {
    DWORD w; HANDLE h;
    if (!h_cbuf || !h_clen) return;
    h = CreateFileA(h_cpath, FILE_APPEND_DATA, FILE_SHARE_READ, 0, OPEN_ALWAYS, 0x80, 0);
    if (h != INVALID_HANDLE_VALUE) { WriteFile(h, h_cbuf, (DWORD)h_clen, &w, 0); CloseHandle(h); }
    h_clen = 0;
}
static void *h_tex;                         /* texture on stage 0 (capture only) */
static void ws_capcam(void);                /* sw2ws.c : camera values into the capture */

typedef int (WINAPI *SetDeclF)(void*, void*);
typedef int (WINAPI *SetFVFF)(void*, u32);
typedef int (WINAPI *DPF)(void*, u32, u32, u32);
typedef int (WINAPI *DIPF)(void*, u32, int, u32, u32, u32, u32);
typedef int (WINAPI *DPUPF)(void*, u32, u32, const void*, u32);
typedef int (WINAPI *DIPUPF)(void*, u32, u32, u32, u32, const void*, u32, const void*, u32);
typedef int (WINAPI *SSSF)(void*, u32, void*, u32, u32);
typedef int (WINAPI *SetVpF)(void*, const u32*);
typedef int (WINAPI *SetScF)(void*, const int*);
typedef int (WINAPI *SetRSF)(void*, u32, u32);
typedef int (WINAPI *SetTexF)(void*, u32, void*);
typedef int (WINAPI *LockF)(void*, u32, u32, void**, u32);
typedef int (WINAPI *UnlockF)(void*);
typedef int (WINAPI *CDF)(void*, u32, u32, HANDLE, u32, u32*, void**);
typedef int (WINAPI *ResetF)(void*, u32*);
typedef int (WINAPI *PresentF)(void*, void*, void*, void*, void*);
static SetDeclF oSetDecl; static SetFVFF oSetFVF;
static DPF oDP; static DIPF oDIP; static DPUPF oDPUP; static DIPUPF oDIPUP;
static SSSF oSSS; static SetVpF oSetVp; static SetScF oSetSc; static SetRSF oSetRS;


static void vhook(void **vt, int idx, void *fn, void **orig) {
    DWORD old; if (vt[idx] == fn) return;
    VirtualProtect(&vt[idx], 4, 0x40, &old); *orig = vt[idx]; vt[idx] = fn; VirtualProtect(&vt[idx], 4, old, &old);
}

/* reference size of the HUD and camera aspect ratio */
static void hud_setsize(int w, int h) {
    if (w <= 0 || h <= 0) return;
    h_W = (float)w; h_H = (float)h; h_cx = h_W * 0.5f;
    h_s = (4.0f / 3.0f) / (h_W / h_H);
    if (h_s > 1.0f) h_s = 1.0f;                  /* 4:3 screen or narrower: nothing to squeeze */
    g_aspect = (float)w / (float)h;
}
/* D3DPRESENT_PARAMETERS: BackBufferWidth, BackBufferHeight first */
static void hud_frompp(const u32 *pp, const char *where) {
    if (!pp || !pp[0] || !pp[1]) return;
    if ((float)pp[0] != h_W || (float)pp[1] != h_H) {
        lg("Game image ("); lg(where); lg("): "); lgn((int)pp[0]); lg("x"); lgn((int)pp[1]); lg(" -> settings adjusted\r\n");
    }
    hud_setsize((int)pp[0], (int)pp[1]);
}

/* each corrected vertex is marked: rhw goes from 1.0 (0x3F800000) to 0x3F800001 (invisible) */
/* elements attached to characters (names, health bars): depth 0.01..0.99999.
   Their position is already right (camera fixed): they are narrowed around their own centre.
   Background bar, fill and name of one character share the same depth -> same anchor. */
static int isworld(float z) { return z > 0.01f && z < 0.99999f; }
static float h_ay[256];
/* group of one character (same depth): centre in X and Y, computed once per frame */
static float anchor(u8 *p, u32 n, u32 stride, u32 zb, float *ay) {
    int k; u32 i; float mn = 1e9f, mx = -1e9f, ymn = 1e9f, ymx = -1e9f;
    for (k = 0; k < h_na; k++) if (h_az[k] == zb) { *ay = h_ay[k]; return h_ax[k]; }
    for (i = 0; i < n; i++) { u8 *v = p + i * stride + h_posoff;
        if (*(u32*)(v + 8) == zb) { float x = *(float*)v, y = *(float*)(v + 4);
            if (x < mn) mn = x; if (x > mx) mx = x; if (y < ymn) ymn = y; if (y > ymx) ymx = y; } }
    *ay = (ymn + ymx) * 0.5f;
    if (h_na < 256) { h_az[h_na] = zb; h_ax[h_na] = (mn + mx) * 0.5f; h_ay[h_na] = *ay; h_na++; }
    return (mn + mx) * 0.5f;
}
#define RHW_ORIG 0x3F800000u
#define RHW_MARK 0x3F800001u

static int fullscreen_vp(void) { return h_vp[0] == 0 && (float)h_vp[2] == h_W; }

/* squeezes n vertices at p. mark=1: vertex buffer (rhw marked, done once); mark=0: our own copy.
   Returns the number of quads moved as screen elements. */
/* per-quad work arrays (one draw at a time, render thread only) */
#define NQ 4096
static float q_mn[NQ], q_mx[NQ], q_y0[NQ], q_y1[NQ]; static unsigned char q_act[NQ], q_side[NQ];
enum { A_DONE, A_RHW, A_FULL, A_OFF, A_VEIL, A_WIDE, A_OVL, A_WORLD, A_SCREEN, A_BLEED };
static const char *q_name[] = { "done", "rhw", "full", "off", "veil", "wide", "overlay", "world", "screen", "bleed" };
static int h_edges = 1;                     /* menu pieces touching a screen side stay against that side */

/* squeezes n vertices at p. mark=1: vertex buffer (rhw marked, done once); mark=0: our own copy.
   Returns the number of quads moved as screen elements. */
static int squeeze_mem(u8 *p, u32 n, u32 stride, int mark, const char *src) {
    u32 i, j, nq = (n + 3) / 4, k; int moved = 0, changed, it;
    if (nq > NQ) nq = NQ;
    if (h_cap) {
        h_draw++;
        cl("#"); clu((u32)h_draw); cl(" "); cl(src); cl(" n="); clu(n);
        cl(" stride="); clu(stride); if (stride >= (u32)h_posoff + 20) { cl(" col="); clx(*(u32*)(p + h_posoff + 16)); }
        cl(" vp="); clu(h_vp[0]); cl(","); clu(h_vp[1]); cl(","); clu(h_vp[2]); cl(","); clu(h_vp[3]);
        if (h_scis_on) { cl(" scissor="); cli(h_scis[0]); cl(","); cli(h_scis[1]); cl(","); cli(h_scis[2]); cl(","); cli(h_scis[3]); }
        cl("\r\n");
    }
    /* 1) what each quad is */
    for (k = 0; k < nq; k++) {
        u32 i0 = k * 4, m = (n - i0 < 4) ? n - i0 : 4, todo = 0;
        float mn = 1e9f, mx = -1e9f, my0 = 1e9f, my1 = -1e9f; u8 *v0 = p + i0 * stride + h_posoff; unsigned char a;
        for (j = 0; j < m; j++) {
            u8 *v = p + (i0 + j) * stride + h_posoff; float x = *(float*)v, y = *(float*)(v + 4);
            if (*(u32*)(v + 12) == RHW_ORIG) todo++;
            if (x < mn) mn = x; if (x > mx) mx = x; if (y < my0) my0 = y; if (y > my1) my1 = y;
        }
        if (!todo) a = (mark && *(u32*)(v0 + 12) == RHW_MARK) ? A_DONE : A_RHW;
        /* covers the whole width. A picture made with margins for wider screens (story mode titles:
           -425..2985 on 2560) still covers everything once squeezed: squeezed, so not stretched */
        else if (mn <= 0.0f && mx >= h_W - 2.0f)
            a = (h_cx + (mn + 0.5f - h_cx) * h_s - 0.5f <= 4.0f && h_cx + (mx + 0.5f - h_cx) * h_s - 0.5f >= h_W - 5.0f) ? A_BLEED : A_FULL;
        /* entirely off screen (e.g. the best-time panel of "Choix du combat", waiting to slide in):
           squeezing would bring it into view -> left where it is */
        else if (mx <= 0.5f || mn >= h_W - 0.5f) a = A_OFF;
        /* full-height translucent veil touching a side (pause menus): kept full width */
        else if (my1 - my0 >= h_H * 0.95f && (mn <= 0.0f || mx >= h_W - 2.0f) && stride >= (u32)h_posoff + 20
                 && (*(u32*)(v0 + 16) >> 24) < 0x60) a = A_VEIL;
        /* tile as wide as the whole screen (scrolling backgrounds, "EXP gagnée"): kept full width */
        else if (mx - mn >= h_W * 0.95f) a = A_WIDE;
        /* tiny overlay in the top-left corner (FPS counters of Steam / drivers): untouched */
        else if (mx < 80.0f && my1 < 40.0f) a = A_OVL;
        else a = isworld(*(float*)(v0 + 8)) ? A_WORLD : A_SCREEN;
        q_mn[k] = mn; q_mx[k] = mx; q_y0[k] = my0; q_y1[k] = my1; q_act[k] = a;
        /* frame pieces in a corner (touching the top or the bottom AND a side: title banners,
           bottom ornaments, side portraits): kept against that side instead of centred */
        q_side[k] = (a == A_SCREEN && h_edges && (my0 <= 8.0f || my1 >= h_H - 8.0f)) ? (mn <= 8.0f ? 1 : mx >= h_W - 8.0f ? 2 : 0) : 0;
    }
    /* 2) other corner pieces joined to them follow (same draw, touching or overlapping) */
    for (it = 0, changed = 1; changed && it < 64 && h_edges; it++) {
        changed = 0;
        for (k = 0; k < nq; k++) {
            if (q_act[k] != A_SCREEN || q_side[k] || !(q_y0[k] <= 8.0f || q_y1[k] >= h_H - 8.0f)) continue;
            for (i = 0; i < nq; i++) {
                if (!q_side[i] || q_act[i] != A_SCREEN) continue;
                if (q_mn[k] <= q_mx[i] + 2.0f && q_mn[i] <= q_mx[k] + 2.0f && q_y0[k] < q_y1[i] && q_y0[i] < q_y1[k]) {
                    q_side[k] = q_side[i]; changed = 1; break;
                }
            }
        }
    }
    /* 3) move */
    for (k = 0; k < nq; k++) {
        u32 i0 = k * 4, m = (n - i0 < 4) ? n - i0 : 4;
        unsigned char a = q_act[k];
        if (a == A_WORLD || a == A_SCREEN || a == A_BLEED) {
            for (j = 0; j < m; j++) {
                u8 *v = p + (i0 + j) * stride + h_posoff;
                if (*(u32*)(v + 12) != RHW_ORIG) continue;
                if (a == A_WORLD) {
                    if (h_world) {
                        /* names / bars: the game places them as if the screen were 4:3 vertically too:
                           their height from the centre is too small by the same factor -> moved up/down
                           as a block (same size), and narrowed around their own centre */
                        float ay, ax = anchor(p, n, stride, *(u32*)(v + 8), &ay);
                        *(float*)v = ax + (*(float*)v - ax) * h_s;
                        *(float*)(v + 4) += (ay - h_H * 0.5f) * (1.0f / h_s - 1.0f);
                    }
                } else if (a == A_BLEED) {                            /* squeezed, edges snapped to the screen sides */
                    float x = h_cx + (*(float*)v + 0.5f - h_cx) * h_s - 0.5f;
                    if (*(float*)v == q_mn[k]) x = q_mn[k] > -0.5f ? q_mn[k] : (x > -0.5f ? -0.5f : x);
                    if (*(float*)v == q_mx[k]) x = x < h_W - 0.5f ? h_W - 0.5f : x;
                    *(float*)v = x;
                } else if (q_side[k] == 1) *(float*)v = (*(float*)v + 0.5f) * h_s - 0.5f;                 /* against the left side */
                else if (q_side[k] == 2) *(float*)v = h_W - (h_W - (*(float*)v + 0.5f)) * h_s - 0.5f;   /* against the right side */
                else *(float*)v = h_cx + (*(float*)v + 0.5f - h_cx) * h_s - 0.5f;                         /* centred */
                if (mark) *(u32*)(v + 12) = RHW_MARK;
            }
            if (a == A_SCREEN) moved++;
        }
        if (h_cap) {
            u8 *v0 = p + i0 * stride + h_posoff;
            cl("  q"); clu(k); cl(" x "); clf(q_mn[k]); cl(" "); clf(q_mx[k]); cl(" y "); clf(q_y0[k]); cl(" "); clf(q_y1[k]);
            cl(" z "); clf(*(float*)(v0 + 8)); cl(" "); cl(q_name[a]);
            if (q_side[k]) cl(q_side[k] == 1 ? " left" : " right");
            cl("\r\n");
        }
    }
    (void)i;
    return moved;
}
/* squeezed scissor rectangle for the draw that follows (restored afterwards) */
static int scis_push(int moved) {
    int r[4];
    if (!moved || !h_scis_on || !h_has_scis || !oSetSc) return 0;
    if (h_scis[0] <= 0 && (float)h_scis[2] >= h_W) return 0;      /* whole width: nothing to do */
    r[0] = (int)(h_cx + ((float)h_scis[0] - h_cx) * h_s);
    r[2] = (int)(h_cx + ((float)h_scis[2] - h_cx) * h_s + 0.999f);
    r[1] = h_scis[1]; r[3] = h_scis[3];
    return oSetSc(h_dev_scis, r) == 0;
}
static void scis_pop(int pushed) { if (pushed) oSetSc(h_dev_scis, h_scis); }

static int active(void) { return h_isT && h_posoff >= 0 && h_fix && h_s < 0.999f && fullscreen_vp(); }

static int squeeze_vb(u32 first, u32 n) {
    u8 *p; int moved;
    if (!active() || !h_vb || h_stride < 16 || n == 0) return 0;
    ONCE(1, "HUD: first 2D batch\r\n");
    if (((LockF)VTBL(h_vb)[11])(h_vb, h_vboff + first * h_stride, n * h_stride, (void**)&p, 0x1000) != 0) { ONCE(2, "HUD: Lock refused\r\n"); return 0; }
    moved = squeeze_mem(p, n, h_stride, 1, "VB");
    ((UnlockF)VTBL(h_vb)[12])(h_vb);
    ONCE(3, "HUD: first batch fixed\r\n");
    return moved;
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
static int WINAPI hSetFVF(void *d, u32 fvf) {
    h_isT = (fvf & 0x400E) == 0x004; h_posoff = h_isT ? 0 : -1;     /* D3DFVF_XYZRHW */
    if (h_isT) ONCE(4, "HUD: SetFVF XYZRHW seen\r\n");
    return oSetFVF(d, fvf);
}
static int WINAPI hSSS(void *d, u32 s, void *vb, u32 off, u32 stride) {
    if (s == 0) { h_vb = vb; h_vboff = off; h_stride = stride; }
    return oSSS(d, s, vb, off, stride);
}
static int WINAPI hSetVp(void *d, const u32 *vp) { if (vp) { int i; for (i = 0; i < 4; i++) h_vp[i] = vp[i]; } return oSetVp(d, vp); }
static int WINAPI hSetSc(void *d, const int *r) { h_dev_scis = d; if (r) { int i; for (i = 0; i < 4; i++) h_scis[i] = r[i]; h_has_scis = 1; } return oSetSc(d, r); }
static int WINAPI hSetRS(void *d, u32 st, u32 v) { if (st == 174) { h_scis_on = v != 0; h_dev_scis = d; } return oSetRS(d, st, v); }   /* D3DRS_SCISSORTESTENABLE */

static u32 nverts(u32 t, u32 pc) { return (t == 4) ? pc * 3 : (t == 5 || t == 6) ? pc + 2 : (t == 2) ? pc * 2 : (t == 3) ? pc + 1 : (t == 1) ? pc : 0; }
static int WINAPI hDP(void *d, u32 t, u32 sv, u32 pc) {
    int r, pu = 0;
    if (h_isT) pu = scis_push(squeeze_vb(sv, nverts(t, pc)));
    else if (h_cap) { h_draw++; cl("#"); clu((u32)h_draw); cl(" 3D DP n="); clu(nverts(t, pc)); cl("\r\n"); }
    r = oDP(d, t, sv, pc); scis_pop(pu); return r;
}
static int WINAPI hDIP(void *d, u32 t, int bv, u32 mi, u32 nv, u32 si, u32 pc) {
    int r, pu = 0;
    if (h_isT) pu = scis_push(squeeze_vb((u32)(bv + (int)mi), nv));
    else if (h_cap) { h_draw++; cl("#"); clu((u32)h_draw); cl(" 3D DIP n="); clu(nv); cl("\r\n"); }
    r = oDIP(d, t, bv, mi, nv, si, pc); scis_pop(pu); return r;
}
/* "UP" draws: vertices in the game's memory -> squeezed in our own copy */
static u8 h_up[256 * 1024];
static int WINAPI hDPUP(void *d, u32 t, u32 pc, const void *vd, u32 stride) {
    u32 n = nverts(t, pc); int r, pu = 0; const void *use = vd;
    if (active() && vd && stride >= 16 && n && n * stride <= sizeof h_up) {
        ONCE(5, "HUD: DrawPrimitiveUP 2D seen\r\n");
        CopyMemory(h_up, vd, n * stride);
        pu = scis_push(squeeze_mem(h_up, n, stride, 0, "UP")); use = h_up;
    }
    r = oDPUP(d, t, pc, use, stride); scis_pop(pu); h_vb = 0;
    return r;
}
static int WINAPI hDIPUP(void *d, u32 t, u32 mi, u32 nv, u32 pc, const void *id, u32 fmt, const void *vd, u32 stride) {
    int r, pu = 0; const void *use = vd;
    if (active() && vd && stride >= 16 && nv && (mi + nv) * stride <= sizeof h_up) {
        ONCE(6, "HUD: DrawIndexedPrimitiveUP 2D seen\r\n");
        CopyMemory(h_up, vd, (mi + nv) * stride);
        pu = scis_push(squeeze_mem(h_up + mi * stride, nv, stride, 0, "IUP")); use = h_up;
    }
    r = oDIPUP(d, t, mi, nv, pc, id, fmt, use, stride); scis_pop(pu); h_vb = 0;
    return r;
}

/* Per-frame work on EndScene (the HD Textures module hooks Present, Reset and SetTexture:
 * hooking the same slots from two mods can make them call each other forever). */
typedef int (WINAPI *EndSceneF)(void*);
static EndSceneF oEndScene; static DWORD h_capt0;
static int WINAPI hEndScene(void *d) {
    int r = oEndScene(d);
    h_na = 0;                                                /* new anchors every frame */
    if (h_cap == 2 && GetTickCount() - h_capt0 > 20) {       /* one whole frame captured */
        cl("=== end of capture ===\r\n\r\n"); capflush(); h_cap = 0;
        lg("F9: capture written (SW2Widescreen_capture.txt)\r\n");
    }
    ws_frame();
    if (h_cap == 1) {                                        /* F9 pressed: capture starts */
        if (!h_cbuf) h_cbuf = (char*)VirtualAlloc(0, CAPMAX, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        h_clen = 0; h_draw = 0; h_capn++; h_capt0 = GetTickCount();
        cl("=== capture "); clu((u32)h_capn); cl(" | screen "); clu((u32)h_W); cl("x"); clu((u32)h_H);
        cl(" | factor "); clf(h_s); cl(" ===\r\n"); ws_capcam();
        h_cap = 2;
    }
    return r;
}
static void hud_hookdev(void *dev) {
    void **vt = VTBL(dev);
    vhook(vt, 42, (void*)hEndScene, (void**)&oEndScene);
    vhook(vt, 47, (void*)hSetVp, (void**)&oSetVp);
    vhook(vt, 57, (void*)hSetRS, (void**)&oSetRS);
    vhook(vt, 75, (void*)hSetSc, (void**)&oSetSc);
    vhook(vt, 81, (void*)hDP, (void**)&oDP);
    vhook(vt, 82, (void*)hDIP, (void**)&oDIP);
    vhook(vt, 83, (void*)hDPUP, (void**)&oDPUP);
    vhook(vt, 84, (void*)hDIPUP, (void**)&oDIPUP);
    vhook(vt, 87, (void*)hSetDecl, (void**)&oSetDecl);
    vhook(vt, 89, (void*)hSetFVF, (void**)&oSetFVF);
    vhook(vt, 100, (void*)hSSS, (void**)&oSSS);
}

/* The game's own call to CreateDevice (in sw2.exe) is redirected to ws_cd, which makes the
 * normal call and then hooks the new device's functions. Nothing is hooked on the Direct3D
 * object itself, so the HD Textures module (which hooks it) can never loop with us. */
/* Borderless window (Borderless=1): the game asks for exclusive fullscreen, which Alt+Tab disturbs
 * (lost device, voices cut during loading). It gets a window without borders covering its monitor
 * instead: same look, but Alt+Tab no longer resets anything. */
static int h_borderless = 0;
static void borderless_window(HWND hw) {
    MONITORINFO mi; HMONITOR mon;
    if (!hw) return;
    mon = MonitorFromWindow(hw, MONITOR_DEFAULTTOPRIMARY);
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoA(mon, &mi)) return;
    SetWindowLongA(hw, GWL_STYLE, (LONG)(WS_POPUP | WS_VISIBLE));
    SetWindowLongA(hw, GWL_EXSTYLE, (LONG)(GetWindowLongA(hw, GWL_EXSTYLE) & ~(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE)));
    SetWindowPos(hw, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                 mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}
static int WINAPI ws_cd(void *d3d, u32 ad, u32 ty, HANDLE w, u32 fl, u32 *pp, void **out) {
    int r = -1; u32 keep[14]; int i, bl = 0;
    if (h_borderless && pp && !pp[8]) {                       /* the game asks for exclusive fullscreen */
        HWND hw = pp[7] ? (HWND)(size_t)pp[7] : (HWND)w;
        for (i = 0; i < 14; i++) keep[i] = pp[i];
        pp[8] = 1; pp[12] = 0;                                 /* windowed, no refresh rate */
        borderless_window(hw);
        r = ((CDF)VTBL(d3d)[16])(d3d, ad, ty, w, fl, pp, out);
        if (r == 0) { bl = 1; borderless_window(hw); lg("Borderless window: on\r\n"); }
        else { for (i = 0; i < 14; i++) pp[i] = keep[i]; lg("Borderless window: refused, exclusive fullscreen kept\r\n"); }
    }
    if (!bl) r = ((CDF)VTBL(d3d)[16])(d3d, ad, ty, w, fl, pp, out);
    lg(r == 0 ? "HUD: game device created\r\n" : "HUD: game device creation failed\r\n");
    if (r == 0 && out && *out && h_fix) {
        h_dev_scis = *out;
        hud_frompp(pp, "CreateDevice");
        hud_hookdev(*out);
    }
    return r;
}
static int hud_install(int w, int h) {
    hud_setsize(w, h);
    return LoadLibraryA("d3d9.dll") != 0;
}
