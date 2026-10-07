// Offline test: loads TCNYCSDL3Pad.asi like the ASI loader would, then replays the DirectInput calls
// True Crime: New York City makes, using the game's own action tables read from tcnyc.exe.
#define DIRECTINPUT_VERSION 0x0800
#define CINTERFACE
#include <windows.h>
#include <dinput.h>
#include <stdio.h>

static const char *EXE = "C:\\Games\\True Crime - New York City\\tcnyc.exe";
static struct { const char *name; DWORD genre, va, n; } g_fmt[3] = {
    {"Walking", 0x0A000000, 0x75d668, 0x3c}, {"Driving", 0x02000000, 0x75dfc8, 0x39}, {"Menu", 0x28000000, 0x75cfd8, 0x2a},
};
static BYTE *g_exe; static DWORD g_exeSize;
static DWORD va2off(DWORD va) {
    if (va >= 0x6cc000 && va < 0x70c000) return va - 0x6cc000 + 0x2cc000;
    if (va >= 0x70c000 && va < 0x777600) return va - 0x70c000 + 0x30c000;
    return 0;
}
static DIACTIONA *load_actions(int k) {
    DIACTIONA *a = calloc(g_fmt[k].n, sizeof *a); DWORD i;
    memcpy(a, g_exe + va2off(g_fmt[k].va), g_fmt[k].n * sizeof *a);
    for (i = 0; i < g_fmt[k].n; i++) a[i].lptszActionName = (LPCSTR)(g_exe + va2off((DWORD)(UINT_PTR)a[i].lptszActionName));
    return a;
}
static void make_format(DIACTIONFORMATA *f, int k, DIACTIONA *acts) {
    memset(f, 0, sizeof *f);
    f->dwSize = sizeof *f; f->dwActionSize = sizeof(DIACTIONA); f->dwDataSize = g_fmt[k].n * 4; f->dwNumActions = g_fmt[k].n;
    f->rgoAction = acts; f->dwGenre = g_fmt[k].genre; f->dwBufferSize = 16; f->lAxisMin = -127; f->lAxisMax = 127;
    memcpy(&f->guidActionMap, g_exe + va2off(0x75ccb0), sizeof(GUID));   // the game's own action-map GUID
    strcpy(f->tszActionMap, g_fmt[k].name);
}

#define MAXDEV 4
static LPDIRECTINPUTDEVICE8A g_dev[MAXDEV]; static DIDEVICEINSTANCEA g_inst[MAXDEV]; static int g_ndev;
static BOOL CALLBACK sem_cb(LPCDIDEVICEINSTANCEA di, LPDIRECTINPUTDEVICE8A dev, DWORD fl, DWORD rem, LPVOID ref) {
    printf("  device: %-40s type %08lX flags %lX remaining %lu\n", di->tszInstanceName, di->dwDevType, fl, rem);
    if (g_ndev < MAXDEV && (fl & DIEDBS_MAPPEDPRI1)) { dev->lpVtbl->AddRef(dev); g_inst[g_ndev] = *di; g_dev[g_ndev++] = dev; }
    return DIENUM_CONTINUE;
}
static LPDIRECTINPUTDEVICE8A g_ff; static LPDIRECTINPUT8A g_di;
static BOOL CALLBACK ff_cb(LPCDIDEVICEINSTANCEA di, LPVOID ref) {
    printf("  FF device offered: %s\n", di->tszInstanceName);
    return FAILED(g_di->lpVtbl->CreateDevice(g_di, &di->guidInstance, &g_ff, NULL)) ? DIENUM_CONTINUE : DIENUM_STOP;
}
static BOOL CALLBACK all_cb(LPCDIDEVICEINSTANCEA di, LPVOID ref) { printf("  all-devices: %-40s type %08lX\n", di->tszInstanceName, di->dwDevType); return DIENUM_CONTINUE; }
static BOOL CALLBACK obj_cb(LPCDIDEVICEOBJECTINSTANCEA o, LPVOID ref) { if (o->dwFlags & DIDOI_FFACTUATOR) (*(int *)ref)++; return DIENUM_CONTINUE; }

int main(int argc, char **argv) {
    HANDLE h; HMODULE asi; HWND wnd; int k, i, seconds = argc > 2 ? atoi(argv[2]) : 3;
    h = CreateFileA(EXE, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("cannot open tcnyc.exe\n"); return 1; }
    g_exeSize = GetFileSize(h, NULL); g_exe = malloc(g_exeSize);
    if (!g_exe || !ReadFile(h, g_exe, g_exeSize, &g_exeSize, NULL)) { printf("cannot read tcnyc.exe\n"); return 1; }
    CloseHandle(h);

    asi = LoadLibraryA(argc > 1 ? argv[1] : "TCNYCSDL3Pad.asi");
    printf("plugin loaded: %p\n", (void *)asi);
    if (!asi) return 1;
    Sleep(1500);   // let SDL find controllers
    wnd = CreateWindowA("STATIC", "harness", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, NULL, NULL);

    if (FAILED(DirectInput8Create(GetModuleHandleA(NULL), 0x800, &IID_IDirectInput8A, (void **)&g_di, NULL))) { printf("DI8 create failed\n"); return 1; }
    printf("EnumDevices(ALL):\n");
    g_di->lpVtbl->EnumDevices(g_di, DI8DEVCLASS_ALL, all_cb, NULL, DIEDFL_ATTACHEDONLY);
    printf("EnumDevices(GAMECTRL, FF) like the game's rumble setup:\n");
    g_di->lpVtbl->EnumDevices(g_di, DI8DEVCLASS_GAMECTRL, ff_cb, NULL, DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    if (g_ff) {
        int nff = 0; LPDIRECTINPUTEFFECT eff = NULL; DWORD axes[2] = {DIJOFS_X, DIJOFS_Y}; LONG dir[2] = {0, 0};
        DICONSTANTFORCE cf = {0}; DIEFFECT e = {0}; HRESULT hr;
        g_ff->lpVtbl->SetDataFormat(g_ff, &c_dfDIJoystick);
        g_ff->lpVtbl->SetCooperativeLevel(g_ff, wnd, DISCL_EXCLUSIVE | DISCL_FOREGROUND);
        g_ff->lpVtbl->EnumObjects(g_ff, obj_cb, &nff, DIDFT_AXIS);
        printf("  force-feedback axes: %d\n", nff);
        g_ff->lpVtbl->Acquire(g_ff);
        e.dwSize = sizeof e; e.dwFlags = DIEFF_CARTESIAN | DIEFF_OBJECTOFFSETS; e.dwDuration = INFINITE; e.dwGain = 10000;
        e.cAxes = nff > 2 ? 2 : nff; e.rgdwAxes = axes; e.rglDirection = dir; e.cbTypeSpecificParams = sizeof cf; e.lpvTypeSpecificParams = &cf;
        hr = g_ff->lpVtbl->CreateEffect(g_ff, &GUID_ConstantForce, &e, &eff, NULL);
        printf("  CreateEffect: %08lX\n", hr);
        if (eff) eff->lpVtbl->Release(eff);   // not started: no rumble during the test
    }

    for (k = 0; k < 3; k++) {
        DIACTIONA *acts = load_actions(k); DIACTIONFORMATA f; DWORD a; int d;
        make_format(&f, k, acts);
        for (d = 0; d < g_ndev; d++) g_dev[d]->lpVtbl->Release(g_dev[d]);
        g_ndev = 0;
        printf("\n=== %s: EnumDevicesBySemantics\n", g_fmt[k].name);
        printf("  hr %08lX\n", g_di->lpVtbl->EnumDevicesBySemantics(g_di, NULL, &f, sem_cb, NULL, 0));
        for (d = 0; d < g_ndev; d++) {
            DIPROPDWORD p = {{sizeof p, sizeof p.diph, 0, DIPH_DEVICE}, 16};
            HRESULT b, s;
            g_dev[d]->lpVtbl->SetCooperativeLevel(g_dev[d], wnd, DISCL_NONEXCLUSIVE | DISCL_BACKGROUND);
            int mine = 0, mineAfter = 0;
            b = g_dev[d]->lpVtbl->BuildActionMap(g_dev[d], &f, NULL, 0);
            for (a = 0; a < f.dwNumActions; a++) mine += IsEqualGUID(&acts[a].guidInstance, &g_inst[d].guidInstance) && acts[a].dwHow;
            s = g_dev[d]->lpVtbl->SetActionMap(g_dev[d], &f, NULL, 0);
            for (a = 0; a < f.dwNumActions; a++) mineAfter += IsEqualGUID(&acts[a].guidInstance, &g_inst[d].guidInstance) && acts[a].dwHow;
            g_dev[d]->lpVtbl->SetProperty(g_dev[d], DIPROP_BUFFERSIZE, &p.diph);
            printf("  %-32s Build %08lX Set %08lX  actions mapped at Set: %d (after Set: %d)\n", g_inst[d].tszInstanceName, b, s, mine, mineAfter);
            for (a = 0; a < f.dwNumActions; a++)
                if (IsEqualGUID(&acts[a].guidInstance, &g_inst[d].guidInstance) && acts[a].dwHow && acts[a].dwSemantic >> 24 != 0x81 && acts[a].dwSemantic >> 24 != 0x82)
                    printf("      app %02X  sem %08lX  obj %08lX  how %02lX  %s\n", (unsigned)acts[a].uAppData, acts[a].dwSemantic, acts[a].dwObjID, acts[a].dwHow, acts[a].lptszActionName);
        }
        // keyboard-borrowed entries must be handed back to the keyboard
        for (a = 0; a < f.dwNumActions; a++)
            if (acts[a].dwSemantic >> 24 == 0x81 && acts[a].dwHow == 0)
                printf("      NOTE keyboard action unmapped: app %02X %s\n", (unsigned)acts[a].uAppData, acts[a].lptszActionName);
        for (d = 0; d < g_ndev; d++) g_dev[d]->lpVtbl->Acquire(g_dev[d]);
        printf("  reading input for %d s (press things on the controller)...\n", k == 0 ? seconds : 1);
        {
            DWORD end = GetTickCount() + (k == 0 ? seconds : 1) * 1000;
            while (GetTickCount() < end) {
                for (d = 0; d < g_ndev; d++) {
                    DIDEVICEOBJECTDATA buf[10]; DWORD n = 10; HRESULT hr;
                    if (FAILED(g_dev[d]->lpVtbl->Poll(g_dev[d]))) { g_dev[d]->lpVtbl->Acquire(g_dev[d]); g_dev[d]->lpVtbl->Poll(g_dev[d]); }
                    hr = g_dev[d]->lpVtbl->GetDeviceData(g_dev[d], sizeof buf[0], buf, &n, 0);
                    if (d == g_ndev - 1) for (i = 0; i < (int)n; i++) printf("    [%s] app %02X = %ld\n", g_inst[d].tszInstanceName, (unsigned)buf[i].uAppData, (LONG)buf[i].dwData);
                    (void)hr;
                }
                Sleep(16);
            }
        }
        for (d = 0; d < g_ndev; d++) g_dev[d]->lpVtbl->Unacquire(g_dev[d]);
    }
    printf("\ndone\n");
    return 0;
}
