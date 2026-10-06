// TCNYCGfxDiag - temporary diagnostic: logs what the game asks Direct3D 8 for (texture formats,
// format checks, failures) to find out why some walls draw black. No behaviour is changed.
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <intrin.h>
#pragma intrinsic(_ReturnAddress, _AddressOfReturnAddress)

static char g_log[MAX_PATH];
static CRITICAL_SECTION g_cs;
static void logf_(const char *fmt, ...) {
    FILE *f; SYSTEMTIME st; va_list a;
    EnterCriticalSection(&g_cs);
    f = fopen(g_log, "a");
    if (f) {
        GetLocalTime(&st);
        fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
        fputc('\n', f); fclose(f);
    }
    LeaveCriticalSection(&g_cs);
}

static const char *fmt_name(DWORD f) {
    static char buf[16];
    switch (f) {
    case 20: return "R8G8B8"; case 21: return "A8R8G8B8"; case 22: return "X8R8G8B8"; case 23: return "R5G6B5";
    case 24: return "X1R5G5B5"; case 25: return "A1R5G5B5"; case 26: return "A4R4G4B4"; case 27: return "R3G3B2";
    case 28: return "A8"; case 29: return "A8R3G3B2"; case 30: return "X4R4G4B4"; case 31: return "A2B10G10R10";
    case 34: return "G16R16"; case 40: return "A8P8"; case 41: return "P8"; case 50: return "L8"; case 51: return "A8L8";
    case 52: return "A4L4"; case 60: return "V8U8"; case 61: return "L6V5U5"; case 62: return "X8L8V8U8";
    case 63: return "Q8W8V8U8"; case 64: return "V16U16"; case 65: return "W11V11U10"; case 67: return "A2W10V10U10";
    case 70: return "D16_LOCKABLE"; case 71: return "D32"; case 73: return "D15S1"; case 75: return "D24S8";
    case 77: return "D24X8"; case 79: return "D24X4S4"; case 80: return "D16";
    case 0x31545844: return "DXT1"; case 0x32545844: return "DXT2"; case 0x33545844: return "DXT3";
    case 0x34545844: return "DXT4"; case 0x35545844: return "DXT5";
    case 0x59565955: return "UYVY"; case 0x32595559: return "YUY2";
    }
    if (f > 0x20202020) { memcpy(buf, &f, 4); buf[4] = 0; return buf; }
    sprintf(buf, "fmt%lu", f); return buf;
}

// texture format histogram
#define NF 64
static struct { DWORD fmt, usage, pool; LONG n, fail; HRESULT lastErr; } g_hist[NF];
static int g_nh;
static void count(DWORD fmt, DWORD usage, DWORD pool, HRESULT hr, const char *kind, UINT w, UINT h, UINT levels) {
    int i;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < g_nh; i++) if (g_hist[i].fmt == fmt && g_hist[i].usage == usage && g_hist[i].pool == pool) break;
    if (i == g_nh && g_nh < NF) { g_hist[i].fmt = fmt; g_hist[i].usage = usage; g_hist[i].pool = pool; g_nh++; }
    if (i < NF) { g_hist[i].n++; if (FAILED(hr)) { g_hist[i].fail++; g_hist[i].lastErr = hr; } }
    LeaveCriticalSection(&g_cs);
    if (FAILED(hr)) logf_("FAILED %s %ux%u levels %u format %s usage %lX pool %lu -> %08lX", kind, w, h, levels, fmt_name(fmt), usage, pool, hr);
}

typedef HRESULT (WINAPI *QI_t)(void *, REFIID, void **);
typedef HRESULT (WINAPI *CreateTex_t)(void *dev, UINT w, UINT h, UINT levels, DWORD usage, DWORD fmt, DWORD pool, void **out);
typedef HRESULT (WINAPI *CreateVol_t)(void *dev, UINT w, UINT h, UINT d, UINT levels, DWORD usage, DWORD fmt, DWORD pool, void **out);
typedef HRESULT (WINAPI *CreateCube_t)(void *dev, UINT edge, UINT levels, DWORD usage, DWORD fmt, DWORD pool, void **out);
typedef HRESULT (WINAPI *CheckFmt_t)(void *d3d, UINT adapter, DWORD devType, DWORD adapterFmt, DWORD usage, DWORD rtype, DWORD fmt);
typedef HRESULT (WINAPI *CreateDev_t)(void *d3d, UINT adapter, DWORD devType, HWND w, DWORD flags, void *pp, void **out);
typedef HRESULT (WINAPI *SetTSS_t)(void *dev, DWORD stage, DWORD type, DWORD value);
static CreateTex_t o_CreateTexture; static CreateVol_t o_CreateVolume; static CreateCube_t o_CreateCube;
static CheckFmt_t o_CheckFmt; static CreateDev_t o_CreateDevice; static SetTSS_t o_SetTSS;
static void *(WINAPI *o_Create8)(UINT);

// who creates render targets, and how big
static volatile LONGLONG g_rtTicks, g_texTicks, g_presentTicks; static volatile DWORD g_presentThread;
static LONG g_rtCreatedTotal, g_rtAtLastFrame; static LONG g_rtPerFrameMax;
#define NRT 32
static struct { void *caller; UINT w, h; DWORD fmt; LONG n; } g_rt[NRT]; static int g_nrt;
static void patch(void **slot, void *hook, void **orig);
// ---- render-target recycling (RenderTargetPool=1) ----
// The game creates and destroys several render-target textures every frame. Instead of letting them be
// destroyed, keep one extra reference; when the game lets go (Release leaves only our reference) the
// texture goes back on a free list and is handed out again for the next request of the same shape.
#define NPOOL 256
typedef ULONG (WINAPI *Release_t)(void *);
typedef ULONG (WINAPI *AddRef_t)(void *);
static struct { void *tex; UINT w, h, levels; DWORD usage, fmt, pool; int inUse; } g_pool[NPOOL];
static int g_npool, g_poolOn; static LONG g_poolHits, g_poolMisses;
static Release_t o_TexRelease; static void **g_texVt;
static ULONG WINAPI h_TexRelease(void *tex) {
    ULONG r = o_TexRelease(tex);
    if (r == 1) {
        int i;
        EnterCriticalSection(&g_cs);
        for (i = 0; i < g_npool; i++) if (g_pool[i].tex == tex && g_pool[i].inUse) { g_pool[i].inUse = 0; break; }
        LeaveCriticalSection(&g_cs);
    }
    return r;
}
static void pool_flush(void) {   // before a device reset every default-pool resource must be gone
    int i, j = 0, freed = 0;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < g_npool; i++) {
        if (!g_pool[i].inUse) { o_TexRelease(g_pool[i].tex); freed++; continue; }
        g_pool[j++] = g_pool[i];
    }
    g_npool = j;
    LeaveCriticalSection(&g_cs);
    logf_("render-target pool flushed before device reset: %d released, %d still held by the game", freed, j);
}
typedef HRESULT (WINAPI *Reset_t)(void *dev, void *pp);
static Reset_t o_Reset;
static HRESULT WINAPI h_Reset(void *dev, void *pp) {
    HRESULT hr;
    if (g_poolOn) pool_flush();
    hr = o_Reset(dev, pp);
    logf_("device Reset -> %08lX", hr);
    return hr;
}
static HRESULT WINAPI h_CreateTexture(void *dev, UINT w, UINT h, UINT levels, DWORD usage, DWORD fmt, DWORD pool, void **out) {
    HRESULT hr;
    if (g_poolOn && (usage & 1) && out) {
        int i;
        EnterCriticalSection(&g_cs);
        for (i = 0; i < g_npool; i++) {
            if (g_pool[i].inUse || g_pool[i].w != w || g_pool[i].h != h || g_pool[i].levels != levels ||
                g_pool[i].usage != usage || g_pool[i].fmt != fmt || g_pool[i].pool != pool) continue;
            g_pool[i].inUse = 1; *out = g_pool[i].tex;
            ((AddRef_t)(*(void ***)g_pool[i].tex)[1])(g_pool[i].tex);   // the game's reference
            g_poolHits++;
            LeaveCriticalSection(&g_cs);
            return 0;
        }
        LeaveCriticalSection(&g_cs);
    }
    {
        LARGE_INTEGER a, b; QueryPerformanceCounter(&a);
        hr = o_CreateTexture(dev, w, h, levels, usage, fmt, pool, out);
        QueryPerformanceCounter(&b);
        if (usage & 1) g_rtTicks += b.QuadPart - a.QuadPart;
        else g_texTicks += b.QuadPart - a.QuadPart;
    }
    count(fmt, usage, pool, hr, "texture", w, h, levels);
    if (g_poolOn && SUCCEEDED(hr) && (usage & 1) && out && *out) {
        void **vt = *(void ***)*out;
        EnterCriticalSection(&g_cs);
        if (!g_texVt) { g_texVt = vt; patch(&vt[2], (void *)h_TexRelease, (void **)&o_TexRelease); }
        if (vt == g_texVt && g_npool < NPOOL) {
            ((AddRef_t)vt[1])(*out);   // our reference keeps it alive after the game lets go
            g_pool[g_npool].tex = *out; g_pool[g_npool].w = w; g_pool[g_npool].h = h; g_pool[g_npool].levels = levels;
            g_pool[g_npool].usage = usage; g_pool[g_npool].fmt = fmt; g_pool[g_npool].pool = pool; g_pool[g_npool].inUse = 1;
            g_npool++;
        }
        g_poolMisses++;
        LeaveCriticalSection(&g_cs);
    }
    if (usage & 1) {
        // the real requester: first return address on the stack that follows a CALL in the game and is
        // outside the game's generic texture-creation routine (0x63FA70-0x63FC20)
        void *c = _ReturnAddress(); int i; DWORD *sp = (DWORD *)_AddressOfReturnAddress();
        for (i = 1; i < 128; i++) {
            DWORD a = sp[i];
            if (a >= 0x401005 && a < 0x6CC000 && !(a >= 0x63FA70 && a < 0x63FC20) && !IsBadReadPtr((void *)(a - 5), 5) && *(BYTE *)(a - 5) == 0xE8) { c = (void *)a; break; }
        }
        EnterCriticalSection(&g_cs);
        for (i = 0; i < g_nrt && !(g_rt[i].caller == c && g_rt[i].w == w && g_rt[i].h == h && g_rt[i].fmt == fmt); i++) ;
        if (i == g_nrt && g_nrt < NRT) { g_rt[i].caller = c; g_rt[i].w = w; g_rt[i].h = h; g_rt[i].fmt = fmt; g_nrt++; }
        if (i < NRT) g_rt[i].n++;
        g_rtCreatedTotal++;
        LeaveCriticalSection(&g_cs);
    }
    return hr;
}
static HRESULT WINAPI h_CreateVolume(void *dev, UINT w, UINT h, UINT d, UINT levels, DWORD usage, DWORD fmt, DWORD pool, void **out) {
    HRESULT hr = o_CreateVolume(dev, w, h, d, levels, usage, fmt, pool, out);
    count(fmt, usage | 0x80000000, pool, hr, "volume texture", w, h, levels);
    return hr;
}
static HRESULT WINAPI h_CreateCube(void *dev, UINT edge, UINT levels, DWORD usage, DWORD fmt, DWORD pool, void **out) {
    HRESULT hr = o_CreateCube(dev, edge, levels, usage, fmt, pool, out);
    count(fmt, usage | 0x40000000, pool, hr, "cube texture", edge, edge, levels);
    return hr;
}
static HRESULT WINAPI h_CheckFmt(void *d3d, UINT adapter, DWORD devType, DWORD adapterFmt, DWORD usage, DWORD rtype, DWORD fmt) {
    static struct { DWORD fmt, usage, rtype; } seen[128]; static int ns;
    HRESULT hr = o_CheckFmt(d3d, adapter, devType, adapterFmt, usage, rtype, fmt);
    int i;
    for (i = 0; i < ns && !(seen[i].fmt == fmt && seen[i].usage == usage && seen[i].rtype == rtype); i++) ;
    if (i == ns && ns < 128) {
        seen[ns].fmt = fmt; seen[ns].usage = usage; seen[ns].rtype = rtype; ns++;
        logf_("CheckDeviceFormat %s usage %lX resource type %lu -> %s (%08lX)", fmt_name(fmt), usage, rtype, SUCCEEDED(hr) ? "supported" : "NOT supported", hr);
    }
    return hr;
}
static HRESULT WINAPI h_SetTSS(void *dev, DWORD stage, DWORD type, DWORD value) {
    // D3D8 colour/alpha ops that have no modern equivalent: bump mapping, premodulate, dot3
    static unsigned char seenOp[2][8][32];
    if ((type == 1 || type == 4) && stage < 8 && value < 32 && !seenOp[type == 4][stage][value]) {
        seenOp[type == 4][stage][value] = 1;
        logf_("texture stage %lu %s op %lu used", stage, type == 1 ? "colour" : "alpha", value);
    }
    return o_SetTSS(dev, stage, type, value);
}
// frame timing between presents
typedef HRESULT (WINAPI *Present_t)(void *dev, const void *src, const void *dst, HWND w, const void *dirty);
static Present_t o_Present;
static LARGE_INTEGER g_qpf, g_lastPresent;
static volatile LONG g_frames, g_hitch25, g_hitch50, g_fgFrames; static double g_sumMs, g_maxMs;
static HRESULT WINAPI h_Present(void *dev, const void *src, const void *dst, HWND w, const void *dirty) {
    LARGE_INTEGER before, now; HRESULT hr;
    QueryPerformanceCounter(&before);
    hr = o_Present(dev, src, dst, w, dirty);
    QueryPerformanceCounter(&now);
    g_presentTicks += now.QuadPart - before.QuadPart; g_presentThread = GetCurrentThreadId();
    if (g_lastPresent.QuadPart) {
        double ms = (now.QuadPart - g_lastPresent.QuadPart) * 1000.0 / g_qpf.QuadPart; LONG rt;
        EnterCriticalSection(&g_cs);
        { DWORD pid = 0; HWND fw = GetForegroundWindow(); if (fw) GetWindowThreadProcessId(fw, &pid); if (pid == GetCurrentProcessId()) g_fgFrames++; }
        g_frames++; g_sumMs += ms; if (ms > g_maxMs) g_maxMs = ms;
        if (ms > 25.0) g_hitch25++;
        if (ms > 50.0) g_hitch50++;
        rt = g_rtCreatedTotal - g_rtAtLastFrame; g_rtAtLastFrame = g_rtCreatedTotal;
        if (rt > g_rtPerFrameMax) g_rtPerFrameMax = rt;
        LeaveCriticalSection(&g_cs);
    }
    g_lastPresent = now;
    return hr;
}
static void patch(void **slot, void *hook, void **orig) {
    DWORD old;
    if (*slot == hook) return;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return;
    *orig = *slot; *slot = hook;
    VirtualProtect(slot, sizeof(void *), old, &old);
}
static HRESULT WINAPI h_CreateDevice(void *d3d, UINT adapter, DWORD devType, HWND w, DWORD flags, void *pp, void **out) {
    HRESULT hr = o_CreateDevice(d3d, adapter, devType, w, flags, pp, out);
    logf_("CreateDevice -> %08lX (behaviour flags %lX)", hr, flags);
    if (SUCCEEDED(hr) && out && *out) {
        void **vt = *(void ***)*out;
        patch(&vt[20], (void *)h_CreateTexture, (void **)&o_CreateTexture);
        patch(&vt[21], (void *)h_CreateVolume, (void **)&o_CreateVolume);
        patch(&vt[22], (void *)h_CreateCube, (void **)&o_CreateCube);
        patch(&vt[63], (void *)h_SetTSS, (void **)&o_SetTSS);
        patch(&vt[15], (void *)h_Present, (void **)&o_Present);
        patch(&vt[14], (void *)h_Reset, (void **)&o_Reset);
    }
    return hr;
}
static void *WINAPI h_Create8(UINT sdk) {
    void *d3d = o_Create8(sdk);
    logf_("Direct3DCreate8(%u) -> %p", sdk, d3d);
    if (d3d) {
        void **vt = *(void ***)d3d;
        patch(&vt[10], (void *)h_CheckFmt, (void **)&o_CheckFmt);
        patch(&vt[15], (void *)h_CreateDevice, (void **)&o_CreateDevice);
    }
    return d3d;
}

static DWORD WINAPI Reporter(LPVOID arg) {
    (void)arg;
    for (;;) {
        int i;
        Sleep(15000);
        EnterCriticalSection(&g_cs);
        if (g_frames)
            logf_("frames: %ld in 15 s (%.1f fps), average %.1f ms, worst %.1f ms, %ld over 25 ms, %ld over 50 ms, most render targets created in one frame %ld, game in front %ld%% of frames",
                  g_frames, g_frames / 15.0, g_sumMs / g_frames, g_maxMs, g_hitch25, g_hitch50, g_rtPerFrameMax, g_fgFrames * 100 / g_frames);
        {
            static ULONGLONG lastCpu; static DWORD lastThread;
            ULONGLONG cpu = 0; FILETIME c, e, k, u;
            HANDLE t = g_presentThread ? OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, g_presentThread) : NULL;
            if (t && GetThreadTimes(t, &c, &e, &k, &u)) cpu = ((ULONGLONG)k.dwHighDateTime << 32 | k.dwLowDateTime) + ((ULONGLONG)u.dwHighDateTime << 32 | u.dwLowDateTime);
            if (t) CloseHandle(t);
            if (g_frames) {
                double f = 1000.0 / g_qpf.QuadPart / g_frames;
                logf_("  per frame: %.1f ms waiting in Present, %.2f ms creating render targets, %.2f ms creating other textures; game thread busy %.0f%% of the time",
                      g_presentTicks * f, g_rtTicks * f, g_texTicks * f,
                      (lastThread == g_presentThread && lastCpu) ? (cpu - lastCpu) / 1e4 / 15000.0 * 100.0 : -1.0);
            }
            lastCpu = cpu; lastThread = g_presentThread; g_presentTicks = g_rtTicks = g_texTicks = 0;
        }
        if (g_poolOn) logf_("render-target pool: %ld reused, %ld newly created, %d kept", g_poolHits, g_poolMisses, g_npool);
        g_poolHits = g_poolMisses = 0;
        g_frames = g_hitch25 = g_hitch50 = g_fgFrames = 0; g_sumMs = g_maxMs = 0; g_rtPerFrameMax = 0;
        for (i = 0; i < g_nh; i++)
            logf_("  textures %-10s usage %8lX pool %lu: %ld created, %ld failed (%08lX)", fmt_name(g_hist[i].fmt), g_hist[i].usage, g_hist[i].pool, g_hist[i].n, g_hist[i].fail, g_hist[i].lastErr);
        for (i = 0; i < g_nrt; i++) {
            char nm[MAX_PATH] = "?", *b; HMODULE m = NULL; DWORD off = 0;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)g_rt[i].caller, &m)) {
                GetModuleFileNameA(m, nm, MAX_PATH); off = (DWORD)((BYTE *)g_rt[i].caller - (BYTE *)m);
            }
            b = strrchr(nm, '\\'); b = b ? b + 1 : nm;
            logf_("  render targets %ux%u %s made by %s+%lX: %ld", g_rt[i].w, g_rt[i].h, fmt_name(g_rt[i].fmt), b, off, g_rt[i].n);
        }
        LeaveCriticalSection(&g_cs);
    }
}

static void **find_import(const char *dll, const char *fn) {
    BYTE *base = (BYTE *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    DWORD rva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    IMAGE_IMPORT_DESCRIPTOR *d;
    if (!rva) return NULL;
    for (d = (IMAGE_IMPORT_DESCRIPTOR *)(base + rva); d->Name; d++) {
        IMAGE_THUNK_DATA *names, *iat;
        if (_stricmp((char *)(base + d->Name), dll)) continue;
        names = (IMAGE_THUNK_DATA *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
        iat = (IMAGE_THUNK_DATA *)(base + d->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++)
            if (!(names->u1.Ordinal & IMAGE_ORDINAL_FLAG) && !strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name, fn))
                return (void **)&iat->u1.Function;
    }
    return NULL;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        char *s; void **slot; DWORD old;
        DisableThreadLibraryCalls(h); InitializeCriticalSection(&g_cs); QueryPerformanceFrequency(&g_qpf);
        {
            char dir[MAX_PATH], ini[MAX_PATH];
            DWORD n = GetModuleFileNameA(h, dir, MAX_PATH);
            if (n == 0 || n >= MAX_PATH) return TRUE;
            s = strrchr(dir, '\\'); if (s) s[1] = 0; else dir[0] = 0;
            snprintf(ini, sizeof ini, "%sTCNYCGfxDiag.ini", dir);
            snprintf(g_log, sizeof g_log, "%sTCNYCGfxDiag.log", dir);
            g_poolOn = GetPrivateProfileIntA("Settings", "RenderTargetPool", 0, ini);
        }
        DeleteFileA(g_log);
        slot = find_import("d3d8.dll", "Direct3DCreate8");
        if (slot && VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) {
            o_Create8 = (void *(WINAPI *)(UINT))*slot; *slot = (void *)h_Create8;
            VirtualProtect(slot, sizeof(void *), old, &old);
            logf_("TCNYCGfxDiag loaded, Direct3DCreate8 hooked (was %p). RenderTargetPool=%d", (void *)o_Create8, g_poolOn);
        } else logf_("TCNYCGfxDiag: Direct3DCreate8 import not found");
        CreateThread(NULL, 0, Reporter, NULL, 0, NULL);
    }
    return TRUE;
}
