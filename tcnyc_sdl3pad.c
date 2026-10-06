// TCNYCSDL3Pad - SDL3 controller support for True Crime: New York City (PC, 2006).
//
// The game never reads a controller directly. It hands DirectInput's "action mapper" a list of
// requests such as "any X axis", "any button 0..11" and "any POV hat", laid out for an original
// Xbox pad, and lets DirectInput decide which physical control does what. On modern pads the mapper
// guesses badly: the right stick axes come out swapped, the triggers share one axis (so Target Lock
// and Fire cannot be held together) and buttons 10/11 (Target Lock / Fire) do not exist at all.
//
// This plugin hides real controllers from the game's DirectInput and offers one virtual controller
// in their place. When the game asks that virtual controller to map its actions, every action is
// placed on the matching SDL3 gamepad input, and the game then receives ordinary DirectInput events.
// The same virtual controller also takes the game's force-feedback effect and turns it into rumble.
#define DIRECTINPUT_VERSION 0x0800
#define CINTERFACE
#include <windows.h>
#include <dinput.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "sdl3_min.h"

#define VERSION "0.4"

// ---------------------------------------------------------------------------------------------
// settings and log
// ---------------------------------------------------------------------------------------------
static char g_dir[MAX_PATH], g_log[MAX_PATH], g_ini[MAX_PATH], g_logTag[48];
static CRITICAL_SECTION g_cs;
static int g_enabled = 1, g_rumbleOn = 1, g_logInput = 0, g_invertAimY = 0, g_swapSticks = 0, g_bgInput = 0, g_cancelGameDz = 1;
static float g_deadzone = 0.15f, g_trigThreshold = 0.30f, g_rumbleScale = 1.0f;

static void logf_(const char *fmt, ...) {
    FILE *f = fopen(g_log, "a"); if (!f) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(f, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    if (g_logTag[0]) fputs(g_logTag, f);
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fputc('\n', f); fclose(f);
}

struct DiagDev;
static struct DiagDev *diag_add(void *dev, const char *name);
static void diag_count(void *dev, HRESULT hr, DWORD n);
static int g_diagOn;
static void text_update(void); static void quit_pictures_prepare(void);
static int g_knownBuild;

// ---------------------------------------------------------------------------------------------
// controller inputs ("sources") the game's actions can be placed on
// ---------------------------------------------------------------------------------------------
enum { AX_LX, AX_LY, AX_RX, AX_RY, AX_LT, AX_RT, AX_RY_INV, NAXIS_SRC };
#define NAXIS     6                       // real axes the virtual controller reports
#define BTN_LT    SDL_GAMEPAD_BUTTON_COUNT       // trigger pulled past TriggerThreshold
#define BTN_RT    (SDL_GAMEPAD_BUTTON_COUNT + 1)
#define NBTN      (SDL_GAMEPAD_BUTTON_COUNT + 2)
#define SRC_NONE  0
#define SRC_AXIS(i) (0x100 | (i))
#define SRC_BTN(i)  (0x200 | (i))
#define SRC_POV     0x300
#define SRC_KIND(s) ((s) & 0xF00)
#define SRC_IDX(s)  ((s) & 0xFF)

static const struct { const char *name; int btn; } g_btnNames[] = {
    {"A", SDL_GAMEPAD_BUTTON_SOUTH}, {"CROSS", SDL_GAMEPAD_BUTTON_SOUTH},
    {"B", SDL_GAMEPAD_BUTTON_EAST}, {"CIRCLE", SDL_GAMEPAD_BUTTON_EAST},
    {"X", SDL_GAMEPAD_BUTTON_WEST}, {"SQUARE", SDL_GAMEPAD_BUTTON_WEST},
    {"Y", SDL_GAMEPAD_BUTTON_NORTH}, {"TRIANGLE", SDL_GAMEPAD_BUTTON_NORTH},
    {"BACK", SDL_GAMEPAD_BUTTON_BACK}, {"VIEW", SDL_GAMEPAD_BUTTON_BACK}, {"SELECT", SDL_GAMEPAD_BUTTON_BACK},
    {"CREATE", SDL_GAMEPAD_BUTTON_BACK}, {"SHARE", SDL_GAMEPAD_BUTTON_BACK},
    {"GUIDE", SDL_GAMEPAD_BUTTON_GUIDE}, {"PS", SDL_GAMEPAD_BUTTON_GUIDE}, {"HOME", SDL_GAMEPAD_BUTTON_GUIDE},
    {"START", SDL_GAMEPAD_BUTTON_START}, {"MENU", SDL_GAMEPAD_BUTTON_START}, {"OPTIONS", SDL_GAMEPAD_BUTTON_START},
    {"LS", SDL_GAMEPAD_BUTTON_LEFT_STICK}, {"L3", SDL_GAMEPAD_BUTTON_LEFT_STICK},
    {"RS", SDL_GAMEPAD_BUTTON_RIGHT_STICK}, {"R3", SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {"LB", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {"L1", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER},
    {"RB", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER}, {"R1", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {"DPAD_UP", SDL_GAMEPAD_BUTTON_DPAD_UP}, {"DPAD_DOWN", SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {"DPAD_LEFT", SDL_GAMEPAD_BUTTON_DPAD_LEFT}, {"DPAD_RIGHT", SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {"MISC", SDL_GAMEPAD_BUTTON_MISC1}, {"MIC", SDL_GAMEPAD_BUTTON_MISC1}, {"MUTE", SDL_GAMEPAD_BUTTON_MISC1},
    {"CAPTURE", SDL_GAMEPAD_BUTTON_MISC1}, {"SHARE_XBOX", SDL_GAMEPAD_BUTTON_MISC1},
    {"PADDLE1", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1}, {"PADDLE2", SDL_GAMEPAD_BUTTON_LEFT_PADDLE1},
    {"PADDLE3", SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2}, {"PADDLE4", SDL_GAMEPAD_BUTTON_LEFT_PADDLE2},
    {"TOUCHPAD", SDL_GAMEPAD_BUTTON_TOUCHPAD},
    {"LT", BTN_LT}, {"L2", BTN_LT}, {"RT", BTN_RT}, {"R2", BTN_RT},
};

// ---------------------------------------------------------------------------------------------
// the game's actions. The game numbers its actions after the buttons of an original Xbox pad,
// and uses the same numbers on foot, in vehicles and in menus (worked out from tcnyc.exe).
// ---------------------------------------------------------------------------------------------
typedef struct { BYTE app; const char *key; const char *def; } ActDef;
static const ActDef g_actFoot[] = {
    {0x24, "LightAttack", "A"},     {0x26, "Grapple", "B"},          {0x25, "HeavyAttack", "X"},
    {0x23, "Jump", "Y"},            {0x1F, "UseAction", "LB"},       {0x20, "ReloadBlock", "RB"},
    {0x1E, "BadgeWarningShot", "BACK"}, {0x1D, "PauseMap", "START"}, {0x03, "Crouch", "LS"},
    {0x18, "PrecisionAim", "RS"},   {0x21, "TargetLock", "LT"},      {0x22, "FireWeapon", "RT"},
    {0x16, "Stealth", ""},
    {0x19, "PrevCombatMode", ""},   {0x1A, "NextCombatMode", ""},
    {0x1B, "Next2ndWeapon", ""},    {0x1C, "NextMainWeapon", ""},
};
static const ActDef g_actDrive[] = {
    {0x24, "Accelerate", "A"},      {0x26, "Handbrake", "B"},        {0x25, "BrakeReverse", "X"},
    {0x23, "LookBehind", "Y"},      {0x1F, "CarDoor", "LB"},         {0x20, "Reload", "RB"},
    {0x1E, "SirenHorn", "BACK"},    {0x1D, "PauseMap", "START"},     {0x03, "LeftStickClick", "LS"},
    {0x18, "PrecisionAim", "RS"},   {0x21, "TargetLock", "LT"},      {0x22, "FireWeapon", "RT"},
    {0x10, "Endo", ""},             {0x11, "Wheelie", ""},
    {0x19, "PrevCameraMode", ""},   {0x1A, "NextCameraMode", ""},
    {0x1B, "RestartSong", ""},      {0x1C, "NextSong", ""},
};
static const ActDef g_actMenu[] = {
    {0x24, "Select", "A"},          {0x26, "ButtonB", "B"},          {0x25, "SetWaypoint", "X"},
    {0x23, "ButtonY", "Y"},         {0x1E, "Back", "BACK"},          {0x1D, "Exit", "START"},
    {0x21, "LeftTrigger", "LT"},    {0x22, "RightTrigger", "RT"},
};
static const struct { const char *section, *label; DWORD genre; const ActDef *acts; int n; } g_modes[3] = {
    {"OnFoot",  "on foot", 0x0A000000, g_actFoot,  sizeof g_actFoot / sizeof g_actFoot[0]},
    {"Driving", "driving", 0x02000000, g_actDrive, sizeof g_actDrive / sizeof g_actDrive[0]},
    {"Menus",   "menus",   0x28000000, g_actMenu,  sizeof g_actMenu / sizeof g_actMenu[0]},
};
static int g_map[3][256];          // per mode: action number -> source
static const char *g_actName[3][256];

static int parse_button(const char *s) {
    char t[32]; int i, n = 0;
    while (*s == ' ' || *s == '\t') s++;
    for (; *s && *s != ' ' && *s != '\t' && *s != ';' && n < 31; s++) t[n++] = (char)toupper((unsigned char)*s);
    t[n] = 0;
    if (!n || !strcmp(t, "NONE")) return SRC_NONE;
    for (i = 0; i < (int)(sizeof g_btnNames / sizeof g_btnNames[0]); i++)
        if (!strcmp(t, g_btnNames[i].name)) return SRC_BTN(g_btnNames[i].btn);
    return -1;
}
static void load_maps(void) {
    int m, i;
    for (m = 0; m < 3; m++) {
        int stickGame = (m != 2);  // stick options only change gameplay, not menu cursor movement
        int moveX = (stickGame && g_swapSticks) ? AX_RX : AX_LX, moveY = (stickGame && g_swapSticks) ? AX_RY : AX_LY;
        int aimX = (stickGame && g_swapSticks) ? AX_LX : AX_RX, aimY = (stickGame && g_swapSticks) ? AX_LY : AX_RY;
        if (stickGame && g_invertAimY && aimY == AX_RY) aimY = AX_RY_INV;
        memset(g_map[m], 0, sizeof g_map[m]);
        g_map[m][0x02] = SRC_AXIS(moveX); g_actName[m][0x02] = m == 2 ? "Cursor Left/Right" : m ? "Steer" : "Move Left/Right";
        g_map[m][0x01] = SRC_AXIS(moveY); g_actName[m][0x01] = m == 2 ? "Cursor Up/Down" : m ? "Gas/Brake" : "Move Forward/Back";
        g_map[m][0x05] = SRC_AXIS(aimX);  g_actName[m][0x05] = "Aim Left/Right";
        g_map[m][0x04] = SRC_AXIS(aimY);  g_actName[m][0x04] = "Aim Up/Down";
        g_map[m][0x07] = SRC_POV;         g_actName[m][0x07] = m == 2 ? "D-pad (menus)" : m ? "D-pad (weapon/song)" : "D-pad (weapon/combat mode)";
        for (i = 0; i < g_modes[m].n; i++) {
            const ActDef *a = &g_modes[m].acts[i]; char v[64]; int s;
            GetPrivateProfileStringA(g_modes[m].section, a->key, a->def, v, sizeof v, g_ini);
            s = parse_button(v);
            if (s < 0) { logf_("[%s] %s=%s is not a button name I know - using %s", g_modes[m].section, a->key, v, a->def[0] ? a->def : "nothing"); s = parse_button(a->def); }
            g_map[m][a->app] = s; g_actName[m][a->app] = a->key;
        }
    }
}

// ---------------------------------------------------------------------------------------------
// SDL3 worker thread: owns SDL, publishes one snapshot of the controller in use
// ---------------------------------------------------------------------------------------------
#define X(ret, name, args) static ret (__cdecl *p##name) args;
SDL_FUNCS(X)
#undef X

typedef struct {
    int connected, live;        // live = connected and the game window has focus
    float axis[NAXIS];          // sticks -1..1 after the dead zone, triggers 0..1
    uint32_t buttons;           // bit per button index (0..NBTN-1)
    uint32_t presses[NBTN];     // count of presses, so taps shorter than a game frame are not lost
} PadSnap;
static PadSnap g_snap;
static volatile LONG g_rumLow, g_rumHigh, g_rumSeq;   // rumble request from the game, 0..65535
static HMODULE g_sdl;

#define MAXPAD 8
static struct { SDL_Gamepad *pad; SDL_JoystickID id; uint32_t prevButtons; } g_pads[MAXPAD];
static int g_active = -1;
static volatile int g_psPad;    // controller in use is a PlayStation pad (for button names)

static int game_has_focus(void) {
    DWORD pid = 0; HWND w = GetForegroundWindow();
    if (w) GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId();
}
static int load_sdl(void) {
    char path[MAX_PATH], exe[MAX_PATH], *s;
    GetModuleFileNameA(NULL, exe, MAX_PATH); s = strrchr(exe, '\\'); if (s) s[1] = 0;
    snprintf(path, MAX_PATH, "%sSDL3.dll", exe);
    g_sdl = LoadLibraryA(path);
    if (!g_sdl) { snprintf(path, MAX_PATH, "%sSDL3.dll", g_dir); g_sdl = LoadLibraryA(path); }
    if (!g_sdl) { logf_("SDL3.dll NOT found (looked next to tcnyc.exe and next to this plugin) - no controller input"); return 0; }
#define X(ret, name, args) p##name = (ret (__cdecl *) args)(void (*)(void))GetProcAddress(g_sdl, #name); \
    if (!p##name) { logf_("SDL3.dll is missing %s - wrong SDL version?", #name); return 0; }
    SDL_FUNCS(X)
#undef X
    return 1;
}
static float stick_dz(float v) { return v < -1.f ? -1.f : v > 1.f ? 1.f : v; }
static void radial(float x, float y, float *ox, float *oy) {
    float m = sqrtf(x * x + y * y);
    if (m <= g_deadzone || m <= 0.f) { *ox = *oy = 0.f; return; }
    float k = (m - g_deadzone) / (1.f - g_deadzone); if (k > 1.f) k = 1.f;
    *ox = stick_dz(x / m * k); *oy = stick_dz(y / m * k);
}
static void add_pad(SDL_JoystickID id) {
    int i;
    for (i = 0; i < MAXPAD; i++) if (g_pads[i].pad && g_pads[i].id == id) return;
    for (i = 0; i < MAXPAD && g_pads[i].pad; i++) ;
    if (i == MAXPAD) return;
    SDL_Gamepad *p = pSDL_OpenGamepad(id);
    if (!p) { logf_("Could not open controller: %s", pSDL_GetError()); return; }
    g_pads[i].pad = p; g_pads[i].id = id; g_pads[i].prevButtons = 0;
    if (g_active < 0) g_active = i;
    logf_("Controller connected: %s (type %s, %04X:%04X)%s", pSDL_GetGamepadName(p),
          pSDL_GetGamepadStringForType(pSDL_GetGamepadType(p)), pSDL_GetGamepadVendor(p), pSDL_GetGamepadProduct(p),
          g_active == i ? " - in use" : "");
}
static void remove_pad(SDL_JoystickID id) {
    int i, j;
    for (i = 0; i < MAXPAD; i++) if (g_pads[i].pad && g_pads[i].id == id) {
        pSDL_CloseGamepad(g_pads[i].pad); g_pads[i].pad = NULL;
        logf_("Controller disconnected");
        if (g_active == i) {
            g_active = -1;
            for (j = 0; j < MAXPAD; j++) if (g_pads[j].pad) { g_active = j; logf_("Now using: %s", pSDL_GetGamepadName(g_pads[j].pad)); break; }
        }
    }
}
static uint32_t read_buttons(SDL_Gamepad *p, const float *ax) {
    uint32_t b = 0; int i;
    for (i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++) if (pSDL_GetGamepadButton(p, i)) b |= 1u << i;
    if (ax[AX_LT] >= g_trigThreshold) b |= 1u << BTN_LT;
    if (ax[AX_RT] >= g_trigThreshold) b |= 1u << BTN_RT;
    return b;
}
static void read_axes(SDL_Gamepad *p, float *ax) {
    float lx = pSDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTX) / 32767.f, ly = pSDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFTY) / 32767.f;
    float rx = pSDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.f, ry = pSDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.f;
    radial(lx, ly, &ax[AX_LX], &ax[AX_LY]);
    radial(rx, ry, &ax[AX_RX], &ax[AX_RY]);
    ax[AX_LT] = pSDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) / 32767.f;
    ax[AX_RT] = pSDL_GetGamepadAxis(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) / 32767.f;
    if (ax[AX_LT] < 0.f) ax[AX_LT] = 0.f;
    if (ax[AX_RT] < 0.f) ax[AX_RT] = 0.f;
}
static DWORD WINAPI Worker(LPVOID arg) {
    LONG sentSeq = -1; DWORD sentAt = 0; int rumbling = 0;
    (void)arg;
    if (!load_sdl()) return 0;
    pSDL_SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
    pSDL_SetMainReady();
    if (!pSDL_Init(SDL_INIT_GAMEPAD)) { logf_("SDL_Init failed: %s", pSDL_GetError()); return 0; }
    { int v = pSDL_GetVersion(); logf_("SDL %d.%d.%d ready", v / 1000000, v / 1000 % 1000, v % 1000); }
    for (;;) {
        SDL_Event e; int i;
        while (pSDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_GAMEPAD_ADDED) add_pad(e.gdevice.which);
            else if (e.type == SDL_EVENT_GAMEPAD_REMOVED) remove_pad(e.gdevice.which);
        }
        // switch to whichever controller was pressed last, so a second idle pad never gets in the way
        for (i = 0; i < MAXPAD; i++) if (g_pads[i].pad) {
            float ax[NAXIS]; uint32_t b;
            read_axes(g_pads[i].pad, ax); b = read_buttons(g_pads[i].pad, ax);
            if ((b & ~g_pads[i].prevButtons) && g_active != i && g_active >= 0) {
                if (rumbling && g_pads[g_active].pad) pSDL_RumbleGamepad(g_pads[g_active].pad, 0, 0, 0);
                g_active = i; sentSeq = -1;
                logf_("Now using: %s", pSDL_GetGamepadName(g_pads[i].pad));
            }
            if (i != g_active) g_pads[i].prevButtons = b;
        }
        {
            PadSnap s; memset(&s, 0, sizeof s);
            uint32_t prev = 0;
            if (g_active >= 0 && g_pads[g_active].pad) {
                int t = pSDL_GetGamepadType(g_pads[g_active].pad);
                g_psPad = t == SDL_GAMEPAD_TYPE_PS3 || t == SDL_GAMEPAD_TYPE_PS4 || t == SDL_GAMEPAD_TYPE_PS5;
                s.connected = 1;
                read_axes(g_pads[g_active].pad, s.axis);
                s.buttons = read_buttons(g_pads[g_active].pad, s.axis);
                prev = g_pads[g_active].prevButtons;
                g_pads[g_active].prevButtons = s.buttons;
            }
            EnterCriticalSection(&g_cs);
            memcpy(s.presses, g_snap.presses, sizeof s.presses);
            for (i = 0; i < NBTN; i++) if ((s.buttons & ~prev) & (1u << i)) s.presses[i]++;
            g_snap = s;
            LeaveCriticalSection(&g_cs);
        }
        // rumble: re-send while active, SDL caps a single request's length
        if (g_active >= 0 && g_pads[g_active].pad) {
            LONG seq = g_rumSeq; DWORD now = GetTickCount();
            LONG lo = g_rumLow, hi = g_rumHigh;
            if (!game_has_focus() && !g_bgInput) lo = hi = 0;
            int want = (lo || hi);
            if (seq != sentSeq || (want && now - sentAt > 1000) || (!want && rumbling)) {
                pSDL_RumbleGamepad(g_pads[g_active].pad, (uint16_t)lo, (uint16_t)hi, want ? 2000 : 0);
                sentSeq = seq; sentAt = now; rumbling = want;
            }
        }
        {
            static DWORD lastText; static int prepared; DWORD now = GetTickCount();
            if (now - lastText > 250) { text_update(); lastText = now; }
            if (!prepared && g_active >= 0) { prepared = 1; quit_pictures_prepare(); }
        }
        pSDL_Delay(2);
    }
}

// ---------------------------------------------------------------------------------------------
// the virtual controller (IDirectInputDevice8A)
// ---------------------------------------------------------------------------------------------
static const GUID GUID_PadInstance = {0x5d3c2a91, 0x7b1e, 0x4f6a, {0x9c, 0x2d, 0x1a, 0x7e, 0x3b, 0x9f, 0x0c, 0x41}};
static const GUID g_zeroGuid = {0};
static const GUID GUID_PadProduct  = {0x5d3c2a92, 0x7b1e, 0x4f6a, {0x9c, 0x2d, 0x1a, 0x7e, 0x3b, 0x9f, 0x0c, 0x41}};
#define PAD_NAME "SDL3 Controller (TCNYCSDL3Pad)"
#define MAXBIND 64
#define MAXSAVE 32

typedef struct { int src; DWORD app, objid; LONG last; uint32_t cnt; } Bind;
typedef struct { int idx; GUID guid; DWORD objid, how; } Saved;
typedef struct Dev {
    IDirectInputDevice8A iface;
    LONG ref;
    int acquired, mode;
    Bind bind[MAXBIND]; int nbind, rot;
    Saved saved[MAXSAVE]; int nsaved;
    LONG amin, amax;
    DWORD seq, bufsize;
} Dev;
#define DEV ((Dev *)This)

static DWORD src_objid(int s) {
    switch (SRC_KIND(s)) {
    case 0x100: { int i = SRC_IDX(s) == AX_RY_INV ? AX_RY : SRC_IDX(s); return DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(i); }
    case 0x200: return DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(SRC_IDX(s));
    case 0x300: return DIDFT_POV | DIDFT_MAKEINSTANCE(0);
    }
    return 0;
}
static int pov_value(uint32_t b) {
    int u = (b >> SDL_GAMEPAD_BUTTON_DPAD_UP) & 1, d = (b >> SDL_GAMEPAD_BUTTON_DPAD_DOWN) & 1;
    int l = (b >> SDL_GAMEPAD_BUTTON_DPAD_LEFT) & 1, r = (b >> SDL_GAMEPAD_BUTTON_DPAD_RIGHT) & 1;
    if (u && d) u = d = 0;
    if (l && r) l = r = 0;
    if (u) return r ? 4500 : l ? 31500 : 0;
    if (d) return r ? 13500 : l ? 22500 : 18000;
    if (r) return 9000;
    if (l) return 27000;
    return -1;
}
static LONG axis_value(const PadSnap *s, int idx, LONG amin, LONG amax) {
    double v;
    if (idx == AX_LT || idx == AX_RT) v = amin + s->axis[idx] * (double)(amax - amin);
    else {
        float f = idx == AX_RY_INV ? -s->axis[AX_RY] : s->axis[idx];
        double half = (amax - amin) / 2.0;
        // The game throws away the first 32/127 of every stick axis (0x40C900) and rescales the rest.
        // Start just past that point so the game's rescale lands exactly on our own (round) dead zone.
        if (g_cancelGameDz && f != 0.f) {
            double dz = half * (32.0 / 127.0), m = fabs(f);
            f = (float)((f < 0 ? -1.0 : 1.0) * (dz + m * (half - dz)) / half);
        }
        v = (amin + amax) / 2.0 + f * half;
    }
    if (v < amin) v = amin;
    if (v > amax) v = amax;
    return (LONG)floor(v + 0.5);
}
static void get_snap(PadSnap *s) {
    EnterCriticalSection(&g_cs); *s = g_snap; LeaveCriticalSection(&g_cs);
    s->live = s->connected;
    if (!g_bgInput && !game_has_focus()) {   // behave like a foreground-only DirectInput device
        uint32_t p[NBTN]; memcpy(p, s->presses, sizeof p);
        memset(s, 0, sizeof *s); memcpy(s->presses, p, sizeof p);
    }
}
static LONG neutral_of(const Bind *b, const PadSnap *s) {
    if (SRC_KIND(b->src) == 0x300) return pov_value(s->buttons);
    if (SRC_KIND(b->src) == 0x200) return (s->buttons >> SRC_IDX(b->src)) & 1 ? 0x80 : 0;
    return 0x7FFFFFFF;  // axes: always report the first value
}

static HRESULT STDMETHODCALLTYPE d_QueryInterface(IDirectInputDevice8A *This, REFIID riid, LPVOID *out) {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDirectInputDevice8A)) {
        *out = This; InterlockedIncrement(&DEV->ref); return S_OK;
    }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE d_AddRef(IDirectInputDevice8A *This) { return InterlockedIncrement(&DEV->ref); }
static void stop_effects_of(void *dev);
static ULONG STDMETHODCALLTYPE d_Release(IDirectInputDevice8A *This) {
    LONG r = InterlockedDecrement(&DEV->ref);
    if (r == 0) { stop_effects_of(This); HeapFree(GetProcessHeap(), 0, This); }
    return r;
}
static HRESULT STDMETHODCALLTYPE d_GetCapabilities(IDirectInputDevice8A *This, LPDIDEVCAPS c) {
    (void)This;
    if (!c || c->dwSize < sizeof(DIDEVCAPS_DX3)) return DIERR_INVALIDPARAM;
    DWORD sz = c->dwSize; memset(c, 0, sz); c->dwSize = sz;
    c->dwFlags = DIDC_ATTACHED | (g_rumbleOn ? DIDC_FORCEFEEDBACK : 0);
    c->dwDevType = DI8DEVTYPE_GAMEPAD | (DI8DEVTYPEGAMEPAD_STANDARD << 8) | DIDEVTYPE_HID;
    c->dwAxes = NAXIS; c->dwButtons = NBTN; c->dwPOVs = 1;
    if (sz >= sizeof(DIDEVCAPS)) { c->dwFFSamplePeriod = 10000; c->dwFFMinTimeResolution = 10000; }
    return DI_OK;
}

// object list for EnumObjects / GetObjectInfo
static const struct { const GUID *g; DWORD ofs; WORD usage; const char *name; } g_axisObj[NAXIS] = {
    {&GUID_XAxis, DIJOFS_X, 0x30, "Left stick X"}, {&GUID_YAxis, DIJOFS_Y, 0x31, "Left stick Y"},
    {&GUID_RxAxis, DIJOFS_RX, 0x33, "Right stick X"}, {&GUID_RyAxis, DIJOFS_RY, 0x34, "Right stick Y"},
    {&GUID_ZAxis, DIJOFS_Z, 0x32, "Left trigger"}, {&GUID_RzAxis, DIJOFS_RZ, 0x35, "Right trigger"},
};
#define NOBJ (NAXIS + NBTN + 1)
static void obj_info(int k, DIDEVICEOBJECTINSTANCEA *o) {
    DWORD sz = o->dwSize; memset(o, 0, sz); o->dwSize = sz;
    if (k < NAXIS) {
        int ff = g_rumbleOn && k < 2;   // the game counts force-feedback axes to decide between 1- and 2-motor rumble
        o->guidType = *g_axisObj[k].g; o->dwOfs = g_axisObj[k].ofs;
        o->dwType = DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(k) | (ff ? DIDFT_FFACTUATOR : 0);
        o->dwFlags = DIDOI_ASPECTPOSITION | (ff ? DIDOI_FFACTUATOR : 0);
        lstrcpynA(o->tszName, g_axisObj[k].name, MAX_PATH);
        if (sz >= sizeof(DIDEVICEOBJECTINSTANCEA)) { o->dwFFMaxForce = ff ? 10000 : 0; o->dwFFForceResolution = ff ? 1 : 0; o->wUsagePage = 1; o->wUsage = g_axisObj[k].usage; }
    } else if (k < NAXIS + NBTN) {
        int b = k - NAXIS;
        o->guidType = GUID_Button; o->dwOfs = DIJOFS_BUTTON(b);
        o->dwType = DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(b);
        wsprintfA(o->tszName, "Button %d", b);
        if (sz >= sizeof(DIDEVICEOBJECTINSTANCEA)) { o->wUsagePage = 9; o->wUsage = (WORD)(b + 1); }
    } else {
        o->guidType = GUID_POV; o->dwOfs = DIJOFS_POV(0);
        o->dwType = DIDFT_POV | DIDFT_MAKEINSTANCE(0);
        lstrcpynA(o->tszName, "D-pad", MAX_PATH);
        if (sz >= sizeof(DIDEVICEOBJECTINSTANCEA)) { o->wUsagePage = 1; o->wUsage = 0x39; }
    }
}
static HRESULT STDMETHODCALLTYPE d_EnumObjects(IDirectInputDevice8A *This, LPDIENUMDEVICEOBJECTSCALLBACKA cb, LPVOID ref, DWORD flags) {
    DWORD types = flags & 0xFF, attrs = flags & 0xFF000000; int k;
    (void)This;
    if (!cb) return DIERR_INVALIDPARAM;
    for (k = 0; k < NOBJ; k++) {
        DIDEVICEOBJECTINSTANCEA o; o.dwSize = sizeof o; obj_info(k, &o);
        if (types && !(o.dwType & types)) continue;
        if ((o.dwType & attrs) != attrs) continue;
        if (cb(&o, ref) == DIENUM_STOP) break;
    }
    return DI_OK;
}
static int find_obj(DWORD obj, DWORD how) {
    int k;
    for (k = 0; k < NOBJ; k++) {
        DIDEVICEOBJECTINSTANCEA o; o.dwSize = sizeof o; obj_info(k, &o);
        if (how == DIPH_BYOFFSET && o.dwOfs == obj) return k;
        if (how == DIPH_BYID && (o.dwType & 0x00FFFFFF) == (obj & 0x00FFFFFF)) return k;
    }
    return -1;
}
static HRESULT STDMETHODCALLTYPE d_GetObjectInfo(IDirectInputDevice8A *This, LPDIDEVICEOBJECTINSTANCEA o, DWORD obj, DWORD how) {
    int k; (void)This;
    if (!o || o->dwSize < sizeof(DIDEVICEOBJECTINSTANCE_DX3A)) return DIERR_INVALIDPARAM;
    if ((k = find_obj(obj, how)) < 0) return DIERR_OBJECTNOTFOUND;
    obj_info(k, o);
    return DI_OK;
}
static void fill_instance(DIDEVICEINSTANCEA *di, DWORD size) {
    memset(di, 0, size); di->dwSize = size;
    di->guidInstance = GUID_PadInstance; di->guidProduct = GUID_PadProduct;
    di->dwDevType = DI8DEVTYPE_GAMEPAD | (DI8DEVTYPEGAMEPAD_STANDARD << 8) | DIDEVTYPE_HID;
    lstrcpynA(di->tszInstanceName, PAD_NAME, MAX_PATH);
    lstrcpynA(di->tszProductName, PAD_NAME, MAX_PATH);
    if (size >= sizeof(DIDEVICEINSTANCEA)) { di->wUsagePage = 1; di->wUsage = 5; if (g_rumbleOn) di->guidFFDriver = GUID_PadProduct; }
}
static HRESULT STDMETHODCALLTYPE d_GetDeviceInfo(IDirectInputDevice8A *This, LPDIDEVICEINSTANCEA di) {
    (void)This;
    if (!di || (di->dwSize != sizeof(DIDEVICEINSTANCEA) && di->dwSize != sizeof(DIDEVICEINSTANCE_DX3A))) return DIERR_INVALIDPARAM;
    fill_instance(di, di->dwSize);
    return DI_OK;
}
// DIPROP_* are small integers cast to GUID pointers
enum { PROP_BUFFERSIZE = 1, PROP_GRANULARITY = 3, PROP_RANGE = 4, PROP_DEADZONE = 5, PROP_SATURATION = 6,
       PROP_FFGAIN = 7, PROP_INSTANCENAME = 13, PROP_PRODUCTNAME = 14, PROP_JOYSTICKID = 15, PROP_VIDPID = 24 };
static HRESULT STDMETHODCALLTYPE d_GetProperty(IDirectInputDevice8A *This, REFGUID prop, LPDIPROPHEADER ph) {
    if (!ph) return DIERR_INVALIDPARAM;
    switch ((UINT_PTR)prop) {
    case PROP_BUFFERSIZE: ((LPDIPROPDWORD)ph)->dwData = DEV->bufsize; return DI_OK;
    case PROP_RANGE: ((LPDIPROPRANGE)ph)->lMin = DEV->amin; ((LPDIPROPRANGE)ph)->lMax = DEV->amax; return DI_OK;
    case PROP_DEADZONE: ((LPDIPROPDWORD)ph)->dwData = (DWORD)(g_deadzone * 10000); return DI_OK;
    case PROP_SATURATION: ((LPDIPROPDWORD)ph)->dwData = 10000; return DI_OK;
    case PROP_GRANULARITY: ((LPDIPROPDWORD)ph)->dwData = 1; return DI_OK;
    case PROP_FFGAIN: ((LPDIPROPDWORD)ph)->dwData = 10000; return DI_OK;
    case PROP_JOYSTICKID: ((LPDIPROPDWORD)ph)->dwData = 0; return DI_OK;
    case PROP_VIDPID: ((LPDIPROPDWORD)ph)->dwData = 0; return DI_OK;
    case PROP_INSTANCENAME: case PROP_PRODUCTNAME:
        MultiByteToWideChar(CP_ACP, 0, PAD_NAME, -1, ((LPDIPROPSTRING)ph)->wsz, MAX_PATH); return DI_OK;
    }
    return DIERR_UNSUPPORTED;
}
static HRESULT STDMETHODCALLTYPE d_SetProperty(IDirectInputDevice8A *This, REFGUID prop, LPCDIPROPHEADER ph) {
    static int dzLogged;
    if (!ph) return DIERR_INVALIDPARAM;
    switch ((UINT_PTR)prop) {
    case PROP_BUFFERSIZE: DEV->bufsize = ((LPCDIPROPDWORD)ph)->dwData; break;
    case PROP_RANGE:
        if (((LPCDIPROPRANGE)ph)->lMin < ((LPCDIPROPRANGE)ph)->lMax) { DEV->amin = ((LPCDIPROPRANGE)ph)->lMin; DEV->amax = ((LPCDIPROPRANGE)ph)->lMax; }
        break;
    case PROP_DEADZONE:   // the game asks for 25%; our own StickDeadzone is used instead
        if (!dzLogged++) logf_("Game asked for a %u%% stick dead zone - using StickDeadzone=%d%% from the ini instead",
                               (unsigned)(((LPCDIPROPDWORD)ph)->dwData / 100), (int)(g_deadzone * 100 + 0.5f));
        break;
    }
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE d_Acquire(IDirectInputDevice8A *This) {
    if (DEV->acquired) return S_FALSE;
    DEV->acquired = 1; return DI_OK;
}
static HRESULT STDMETHODCALLTYPE d_Unacquire(IDirectInputDevice8A *This) {
    if (!DEV->acquired) return DI_NOEFFECT;
    DEV->acquired = 0; stop_effects_of(This);   // DirectInput stops a device's effects when it is let go
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE d_GetDeviceState(IDirectInputDevice8A *This, DWORD cb, LPVOID data) {
    PadSnap s; int i;
    if (!data) return DIERR_INVALIDPARAM;
    if (!DEV->acquired) return DIERR_NOTACQUIRED;
    memset(data, 0, cb);
    if (cb != sizeof(DIJOYSTATE) && cb != sizeof(DIJOYSTATE2)) return DI_OK;
    get_snap(&s);
    {
        DIJOYSTATE *j = data; LONG lo = DEV->amin, hi = DEV->amax;
        j->lX = axis_value(&s, AX_LX, lo, hi); j->lY = axis_value(&s, AX_LY, lo, hi);
        j->lZ = axis_value(&s, AX_LT, lo, hi); j->lRx = axis_value(&s, AX_RX, lo, hi);
        j->lRy = axis_value(&s, AX_RY, lo, hi); j->lRz = axis_value(&s, AX_RT, lo, hi);
        j->rgdwPOV[0] = (DWORD)pov_value(s.buttons); j->rgdwPOV[1] = j->rgdwPOV[2] = j->rgdwPOV[3] = (DWORD)-1;
        for (i = 0; i < NBTN; i++) j->rgbButtons[i] = (s.buttons >> i) & 1 ? 0x80 : 0;
    }
    return DI_OK;
}
static void emit(Dev *d, LPDIDEVICEOBJECTDATA buf, DWORD cb, DWORD n, const Bind *b, LONG value) {
    d->seq++;
    if (g_logInput) logf_("  %s = %ld", g_actName[d->mode][b->app & 0xFF] ? g_actName[d->mode][b->app & 0xFF] : "?", value);
    if (!buf) return;
    DIDEVICEOBJECTDATA *o = (DIDEVICEOBJECTDATA *)((BYTE *)buf + n * cb);
    o->dwOfs = b->objid; o->dwData = (DWORD)value; o->dwTimeStamp = GetTickCount(); o->dwSequence = d->seq;
    if (cb >= sizeof(DIDEVICEOBJECTDATA)) o->uAppData = b->app;
}
static HRESULT STDMETHODCALLTYPE d_GetDeviceData(IDirectInputDevice8A *This, DWORD cb, LPDIDEVICEOBJECTDATA buf, LPDWORD inout, DWORD flags) {
    Dev *d = DEV; PadSnap s; DWORD max, n = 0; int k, peek = (flags & DIGDD_PEEK) != 0, stop = 0;
    Bind save[MAXBIND];
    if (!inout || (cb != sizeof(DIDEVICEOBJECTDATA) && cb != sizeof(DIDEVICEOBJECTDATA_DX3))) return DIERR_INVALIDPARAM;
    if (g_diagOn) diag_add(This, "SDL3 controller");
    if (!d->acquired) { diag_count(This, DIERR_NOTACQUIRED, 0); return DIERR_NOTACQUIRED; }
    max = buf ? *inout : 0xFFFFFFFF;
    if (!d->nbind) { *inout = 0; diag_count(This, DI_OK, 0); return DI_OK; }
    if (peek) memcpy(save, d->bind, sizeof(Bind) * d->nbind);
    get_snap(&s);
    // Report each mapped control whose value differs from what the game last saw. Nothing queues up,
    // so the game never receives stale input and a full buffer can never swallow a button release.
    for (k = 0; k < d->nbind && !stop; k++) {
        int i = (d->rot + k) % d->nbind; Bind *b = &d->bind[i];
        if (SRC_KIND(b->src) == 0x200) {
            int idx = SRC_IDX(b->src);
            int want = ((s.buttons >> idx) & 1) || (s.live && s.presses[idx] != b->cnt);
            LONG v = want ? 0x80 : 0;
            if (v != b->last) { if (n >= max) { stop = 1; d->rot = i; break; } emit(d, buf, cb, n++, b, v); b->last = v; }
            if (want || !s.live) b->cnt = s.presses[idx];   // presses made while the game is in the background are dropped
        } else if (SRC_KIND(b->src) == 0x300) {
            LONG v = pov_value(s.buttons);
            if (v != b->last) {
                // The game adds hat directions together without clearing them, so it must see "centred" in between.
                if (b->last != -1 && v != -1) { if (n >= max) { stop = 1; d->rot = i; break; } emit(d, buf, cb, n++, b, -1); b->last = -1; }
                if (n >= max) { stop = 1; d->rot = i; break; }
                emit(d, buf, cb, n++, b, v); b->last = v;
            }
        } else {
            LONG v = axis_value(&s, SRC_IDX(b->src), d->amin, d->amax);
            if (v != b->last) { if (n >= max) { stop = 1; d->rot = i; break; } emit(d, buf, cb, n++, b, v); b->last = v; }
        }
    }
    if (peek) memcpy(d->bind, save, sizeof(Bind) * d->nbind);
    *inout = n;
    diag_count(This, DI_OK, n);
    return DI_OK;   // nothing is ever lost: whatever did not fit is reported on the next read
}
static HRESULT STDMETHODCALLTYPE d_SetDataFormat(IDirectInputDevice8A *This, LPCDIDATAFORMAT f) {
    if (!f) return DIERR_INVALIDPARAM;
    DEV->nbind = 0; DEV->amin = 0; DEV->amax = 65535;
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE d_SetEventNotification(IDirectInputDevice8A *This, HANDLE h) { (void)This; (void)h; return DI_OK; }
static HRESULT STDMETHODCALLTYPE d_SetCooperativeLevel(IDirectInputDevice8A *This, HWND w, DWORD f) { (void)This; (void)w; (void)f; return DI_OK; }
static HRESULT STDMETHODCALLTYPE d_RunControlPanel(IDirectInputDevice8A *This, HWND w, DWORD f) { (void)This; (void)w; (void)f; return DI_OK; }
static HRESULT STDMETHODCALLTYPE d_Initialize(IDirectInputDevice8A *This, HINSTANCE h, DWORD v, REFGUID g) {
    (void)This; (void)h; (void)v;
    return g && IsEqualGUID(g, &GUID_PadInstance) ? DI_OK : DIERR_DEVICENOTREG;
}

// ---------------------------------------------------------------------------------------------
// force feedback -> rumble (the game makes one constant-force effect: direction = [left, right])
// ---------------------------------------------------------------------------------------------
typedef struct Eff {
    IDirectInputEffect iface;
    LONG ref; GUID guid;
    LONG mag, dir[2]; DWORD naxes, gain; int playing;
    void *dev;                  // device that made it (only compared, never used)
} Eff;
#define EFF ((Eff *)This)
static Eff *volatile g_rumOwner;

static void rumble_set(LONG lo, LONG hi) {
    if (lo < 0) lo = 0;
    if (hi < 0) hi = 0;
    if (lo > 65535) lo = 65535;
    if (hi > 65535) hi = 65535;
    if (lo != g_rumLow || hi != g_rumHigh) { g_rumLow = lo; g_rumHigh = hi; InterlockedIncrement(&g_rumSeq); }
}
static void eff_apply(Eff *e) {
    if (!e->playing) { if (g_rumOwner == e) { rumble_set(0, 0); g_rumOwner = NULL; } return; }
    double mag = labs(e->mag) * (e->gain / 10000.0), lo = mag, hi = mag;
    if (e->naxes >= 2) {
        double a = labs(e->dir[0]), b = labs(e->dir[1]), len = sqrt(a * a + b * b);
        if (len > 0) { lo = mag * a / len; hi = mag * b / len; }
    }
    lo = lo / 10000.0 * 65535.0 * g_rumbleScale; hi = hi / 10000.0 * 65535.0 * g_rumbleScale;
    g_rumOwner = e;
    rumble_set((LONG)lo, (LONG)hi);
}
static void stop_effects_of(void *dev) {
    Eff *e = g_rumOwner;
    if (e && e->dev == dev) { e->playing = 0; eff_apply(e); }
}
static HRESULT STDMETHODCALLTYPE e_QueryInterface(IDirectInputEffect *This, REFIID riid, LPVOID *out) {
    if (!out) return E_POINTER;
    if (IsEqualIID(riid, &IID_IUnknown) || IsEqualIID(riid, &IID_IDirectInputEffect)) { *out = This; InterlockedIncrement(&EFF->ref); return S_OK; }
    *out = NULL; return E_NOINTERFACE;
}
static ULONG STDMETHODCALLTYPE e_AddRef(IDirectInputEffect *This) { return InterlockedIncrement(&EFF->ref); }
static ULONG STDMETHODCALLTYPE e_Release(IDirectInputEffect *This) {
    LONG r = InterlockedDecrement(&EFF->ref);
    if (r == 0) { EFF->playing = 0; eff_apply(EFF); HeapFree(GetProcessHeap(), 0, This); }
    return r;
}
static HRESULT STDMETHODCALLTYPE e_Initialize(IDirectInputEffect *This, HINSTANCE h, DWORD v, REFGUID g) { (void)This; (void)h; (void)v; (void)g; return DI_OK; }
static HRESULT STDMETHODCALLTYPE e_GetEffectGuid(IDirectInputEffect *This, LPGUID g) { if (!g) return E_POINTER; *g = EFF->guid; return DI_OK; }
static HRESULT STDMETHODCALLTYPE e_GetParameters(IDirectInputEffect *This, LPDIEFFECT p, DWORD flags) {
    if (!p) return DIERR_INVALIDPARAM;
    if (flags & DIEP_GAIN) p->dwGain = EFF->gain;
    if (flags & DIEP_DURATION) p->dwDuration = INFINITE;
    if ((flags & DIEP_TYPESPECIFICPARAMS) && p->lpvTypeSpecificParams && p->cbTypeSpecificParams >= sizeof(DICONSTANTFORCE))
        ((DICONSTANTFORCE *)p->lpvTypeSpecificParams)->lMagnitude = EFF->mag;
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE e_SetParameters(IDirectInputEffect *This, LPCDIEFFECT p, DWORD flags) {
    Eff *e = EFF;
    if (!p) return DIERR_INVALIDPARAM;
    if (flags & DIEP_AXES) e->naxes = p->cAxes;
    if (flags & DIEP_GAIN) e->gain = p->dwGain > 10000 ? 10000 : p->dwGain;
    if ((flags & DIEP_DIRECTION) && p->rglDirection && p->cAxes >= 1) {
        e->dir[0] = p->rglDirection[0]; e->dir[1] = p->cAxes >= 2 ? p->rglDirection[1] : 0;
        if (!(flags & DIEP_AXES) && p->cAxes > e->naxes) e->naxes = p->cAxes;
    }
    if ((flags & DIEP_TYPESPECIFICPARAMS) && p->lpvTypeSpecificParams && p->cbTypeSpecificParams >= sizeof(DICONSTANTFORCE))
        e->mag = ((const DICONSTANTFORCE *)p->lpvTypeSpecificParams)->lMagnitude;
    if (flags & DIEP_START) e->playing = 1;
    if (e->playing) eff_apply(e);
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE e_Start(IDirectInputEffect *This, DWORD it, DWORD flags) { (void)it; (void)flags; EFF->playing = 1; eff_apply(EFF); return DI_OK; }
static HRESULT STDMETHODCALLTYPE e_Stop(IDirectInputEffect *This) { EFF->playing = 0; eff_apply(EFF); return DI_OK; }
static HRESULT STDMETHODCALLTYPE e_GetEffectStatus(IDirectInputEffect *This, LPDWORD st) { if (!st) return E_POINTER; *st = EFF->playing ? DIEGES_PLAYING : 0; return DI_OK; }
static HRESULT STDMETHODCALLTYPE e_Download(IDirectInputEffect *This) { (void)This; return DI_OK; }
static HRESULT STDMETHODCALLTYPE e_Unload(IDirectInputEffect *This) { EFF->playing = 0; eff_apply(EFF); return DI_OK; }
static HRESULT STDMETHODCALLTYPE e_Escape(IDirectInputEffect *This, LPDIEFFESCAPE x) { (void)This; (void)x; return DIERR_UNSUPPORTED; }
static IDirectInputEffectVtbl g_effVtbl = {
    e_QueryInterface, e_AddRef, e_Release, e_Initialize, e_GetEffectGuid, e_GetParameters, e_SetParameters,
    e_Start, e_Stop, e_GetEffectStatus, e_Download, e_Unload, e_Escape,
};

static HRESULT STDMETHODCALLTYPE d_CreateEffect(IDirectInputDevice8A *This, REFGUID g, LPCDIEFFECT p, LPDIRECTINPUTEFFECT *out, LPUNKNOWN outer) {
    static int logged;
    (void)This; (void)outer;
    if (!out) return DIERR_INVALIDPARAM;
    *out = NULL;
    if (!g_rumbleOn) return DIERR_UNSUPPORTED;
    Eff *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Eff));
    if (!e) return DIERR_OUTOFMEMORY;
    e->iface.lpVtbl = &g_effVtbl; e->ref = 1; e->guid = g ? *g : GUID_ConstantForce; e->dev = This; e->gain = 10000; e->naxes = 1;
    if (p) e_SetParameters(&e->iface, p, DIEP_AXES | DIEP_GAIN | DIEP_DIRECTION | DIEP_TYPESPECIFICPARAMS);
    if (!logged++) logf_("Game set up rumble (%lu motor%s)", e->naxes, e->naxes == 1 ? "" : "s");
    *out = &e->iface;
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE d_EnumEffects(IDirectInputDevice8A *This, LPDIENUMEFFECTSCALLBACKA cb, LPVOID ref, DWORD type) {
    DIEFFECTINFOA ei; (void)This;
    if (!cb) return DIERR_INVALIDPARAM;
    if (!g_rumbleOn) return DI_OK;
    if (type != DIEFT_ALL && DIEFT_GETTYPE(type) != DIEFT_CONSTANTFORCE) return DI_OK;
    memset(&ei, 0, sizeof ei); ei.dwSize = sizeof ei; ei.guid = GUID_ConstantForce; ei.dwEffType = DIEFT_CONSTANTFORCE;
    ei.dwStaticParams = ei.dwDynamicParams = DIEP_AXES | DIEP_DIRECTION | DIEP_GAIN | DIEP_TYPESPECIFICPARAMS | DIEP_DURATION;
    lstrcpynA(ei.tszName, "Constant Force", MAX_PATH);
    cb(&ei, ref);
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE d_GetEffectInfo(IDirectInputDevice8A *This, LPDIEFFECTINFOA ei, REFGUID g) {
    (void)This;
    if (!ei || !g) return DIERR_INVALIDPARAM;
    if (!g_rumbleOn || !IsEqualGUID(g, &GUID_ConstantForce)) return DIERR_DEVICENOTREG;
    memset(ei, 0, sizeof *ei); ei->dwSize = sizeof *ei; ei->guid = GUID_ConstantForce; ei->dwEffType = DIEFT_CONSTANTFORCE;
    lstrcpynA(ei->tszName, "Constant Force", MAX_PATH);
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE d_GetForceFeedbackState(IDirectInputDevice8A *This, LPDWORD st) {
    (void)This;
    if (!st) return E_POINTER;
    *st = DIGFFS_POWERON | DIGFFS_ACTUATORSON | (g_rumLow || g_rumHigh ? 0 : DIGFFS_STOPPED);
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE d_SendForceFeedbackCommand(IDirectInputDevice8A *This, DWORD cmd) {
    (void)This;
    if (cmd & (DISFFC_RESET | DISFFC_STOPALL)) { if (g_rumOwner) g_rumOwner->playing = 0; rumble_set(0, 0); g_rumOwner = NULL; }
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE d_EnumCreatedEffectObjects(IDirectInputDevice8A *This, LPDIENUMCREATEDEFFECTOBJECTSCALLBACK cb, LPVOID ref, DWORD f) { (void)This; (void)cb; (void)ref; (void)f; return DI_OK; }
static HRESULT STDMETHODCALLTYPE d_Escape(IDirectInputDevice8A *This, LPDIEFFESCAPE x) { (void)This; (void)x; return DIERR_UNSUPPORTED; }
static HRESULT STDMETHODCALLTYPE d_Poll(IDirectInputDevice8A *This) { return DEV->acquired ? DI_OK : DIERR_NOTACQUIRED; }
static HRESULT STDMETHODCALLTYPE d_SendDeviceData(IDirectInputDevice8A *This, DWORD cb, LPCDIDEVICEOBJECTDATA d, LPDWORD n, DWORD f) { (void)This; (void)cb; (void)d; (void)n; (void)f; return DIERR_UNSUPPORTED; }
static HRESULT STDMETHODCALLTYPE d_EnumEffectsInFile(IDirectInputDevice8A *This, LPCSTR fn, LPDIENUMEFFECTSINFILECALLBACK cb, LPVOID ref, DWORD f) { (void)This; (void)fn; (void)cb; (void)ref; (void)f; return DIERR_UNSUPPORTED; }
static HRESULT STDMETHODCALLTYPE d_WriteEffectToFile(IDirectInputDevice8A *This, LPCSTR fn, DWORD n, LPDIFILEEFFECT e, DWORD f) { (void)This; (void)fn; (void)n; (void)e; (void)f; return DIERR_UNSUPPORTED; }

// ---------------------------------------------------------------------------------------------
// action mapping: place each of the game's actions on its button
// ---------------------------------------------------------------------------------------------
static int mode_of(DWORD genre) {
    int m;
    for (m = 0; m < 3; m++) if ((genre & 0xFF000000) == g_modes[m].genre) return m;
    return -1;
}
static int is_kbm_semantic(DWORD sem) { DWORD g = sem >> 24; return g == 0x81 || g == 0x82 || g == 0x83; }
static int sem_kind(DWORD sem) { DWORD t = sem & 0x600; return t == 0x200 ? 0x100 : t == 0x400 ? 0x200 : t == 0x600 ? 0x300 : 0; }

static HRESULT STDMETHODCALLTYPE d_BuildActionMap(IDirectInputDevice8A *This, LPDIACTIONFORMATA f, LPCSTR user, DWORD flags) {
    Dev *d = DEV; DWORD i; int m, mapped = 0, pass;
    unsigned char claimed[256], mine[512];
    (void)user; (void)flags;
    if (!f || !f->rgoAction || f->dwActionSize != sizeof(DIACTIONA)) return DIERR_INVALIDPARAM;
    m = mode_of(f->dwGenre);
    if (m < 0) { static int warned; if (!warned++) logf_("Unknown control set (genre %08lX) - using the on-foot layout", f->dwGenre); m = 0; }
    d->mode = m; d->nsaved = 0;
    memset(claimed, 0, sizeof claimed); memset(mine, 0, sizeof mine);
    // First pass takes the game's own controller entries, second pass takes keyboard-only actions
    // the ini puts on a button (Stealth and friends). Each action is taken once.
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < f->dwNumActions && i < 512; i++) {
            DIACTIONA *a = &f->rgoAction[i]; UINT_PTR app = a->uAppData; int src;
            if (app >= 256 || claimed[app] || !(src = g_map[m][app])) continue;
            if (pass == 0 && (is_kbm_semantic(a->dwSemantic) || sem_kind(a->dwSemantic) != SRC_KIND(src))) continue;
            if (pass == 1 && (SRC_KIND(src) != 0x200 || d->nsaved >= MAXSAVE)) continue;
            if (pass == 1 && d->nsaved < MAXSAVE) {
                // borrowed from the keyboard: remembered and handed back in SetActionMap
                Saved *sv = &d->saved[d->nsaved++]; sv->idx = (int)i; sv->guid = a->guidInstance; sv->objid = a->dwObjID; sv->how = a->dwHow;
            }
            a->guidInstance = GUID_PadInstance; a->dwObjID = src_objid(src); a->dwHow = DIAH_DEFAULT;
            claimed[app] = 1; mine[i] = 1; mapped++;
        }
    for (i = 0; i < f->dwNumActions && i < 512; i++) {
        DIACTIONA *a = &f->rgoAction[i];
        if (!mine[i] && IsEqualGUID(&a->guidInstance, &GUID_PadInstance)) { a->guidInstance = g_zeroGuid; a->dwObjID = 0; a->dwHow = DIAH_UNMAPPED; }
    }
    return mapped ? DI_OK : DI_NOEFFECT;
}
static HRESULT STDMETHODCALLTYPE d_SetActionMap(IDirectInputDevice8A *This, LPDIACTIONFORMATA f, LPCSTR user, DWORD flags) {
    Dev *d = DEV; DWORD i; PadSnap s; int k;
    static DWORD lastGenre; static int nlog;
    (void)user; (void)flags;
    if (!f || !f->rgoAction || f->dwActionSize != sizeof(DIACTIONA)) return DIERR_INVALIDPARAM;
    d->mode = mode_of(f->dwGenre); if (d->mode < 0) d->mode = 0;
    d->amin = f->lAxisMin; d->amax = f->lAxisMax;
    if (d->amin >= d->amax) { d->amin = 0; d->amax = 65535; }
    if (f->dwBufferSize) d->bufsize = f->dwBufferSize;
    get_snap(&s);
    d->nbind = 0; d->rot = 0;
    for (i = 0; i < f->dwNumActions; i++) {
        DIACTIONA *a = &f->rgoAction[i]; int src;
        if (!IsEqualGUID(&a->guidInstance, &GUID_PadInstance) || !a->dwHow || (a->dwHow & DIAH_ERROR) || a->uAppData >= 256) continue;
        if (!(src = g_map[d->mode][a->uAppData]) || src_objid(src) != a->dwObjID || d->nbind >= MAXBIND) continue;
        Bind *b = &d->bind[d->nbind++];
        b->src = src; b->app = (DWORD)a->uAppData; b->objid = a->dwObjID;
        b->last = neutral_of(b, &s);   // held buttons do not count as new presses in the new mode
        b->cnt = SRC_KIND(src) == 0x200 ? s.presses[SRC_IDX(src)] : 0;
    }
    for (k = 0; k < d->nsaved; k++) {
        DIACTIONA *a = &f->rgoAction[d->saved[k].idx];
        a->guidInstance = d->saved[k].guid; a->dwObjID = d->saved[k].objid; a->dwHow = d->saved[k].how;
    }
    d->nsaved = 0;
    if (f->dwGenre != lastGenre || nlog < 3) {
        logf_("Controls set: %s (%d actions on the controller, axis range %ld..%ld)", g_modes[d->mode].label, d->nbind, d->amin, d->amax);
        if (f->dwGenre != lastGenre) nlog = 0;
        nlog++; lastGenre = f->dwGenre;
    }
    return DI_OK;
}
static HRESULT STDMETHODCALLTYPE d_GetImageInfo(IDirectInputDevice8A *This, LPDIDEVICEIMAGEINFOHEADERA h) { (void)This; (void)h; return DIERR_UNSUPPORTED; }

static IDirectInputDevice8AVtbl g_devVtbl = {
    d_QueryInterface, d_AddRef, d_Release, d_GetCapabilities, d_EnumObjects, d_GetProperty, d_SetProperty,
    d_Acquire, d_Unacquire, d_GetDeviceState, d_GetDeviceData, d_SetDataFormat, d_SetEventNotification,
    d_SetCooperativeLevel, d_GetObjectInfo, d_GetDeviceInfo, d_RunControlPanel, d_Initialize, d_CreateEffect,
    d_EnumEffects, d_GetEffectInfo, d_GetForceFeedbackState, d_SendForceFeedbackCommand,
    d_EnumCreatedEffectObjects, d_Escape, d_Poll, d_SendDeviceData, d_EnumEffectsInFile, d_WriteEffectToFile,
    d_BuildActionMap, d_SetActionMap, d_GetImageInfo,
};
static Dev *dev_new(void) {
    Dev *d = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(Dev));
    if (!d) return NULL;
    d->iface.lpVtbl = &g_devVtbl; d->ref = 1; d->amin = 0; d->amax = 65535; d->bufsize = 16;
    return d;
}

// ---------------------------------------------------------------------------------------------
// DirectInput hooks: hide real controllers from the game and offer the virtual one
// ---------------------------------------------------------------------------------------------
typedef HRESULT (WINAPI *DI8Create_fn)(HINSTANCE, DWORD, REFIID, LPVOID *, LPUNKNOWN);
static DI8Create_fn o_DI8Create;
static HRESULT (STDMETHODCALLTYPE *o_CreateDevice)(IDirectInput8A *, REFGUID, LPDIRECTINPUTDEVICE8A *, LPUNKNOWN);
static HRESULT (STDMETHODCALLTYPE *o_EnumDevices)(IDirectInput8A *, DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD);
static HRESULT (STDMETHODCALLTYPE *o_GetDeviceStatus)(IDirectInput8A *, REFGUID);
static HRESULT (STDMETHODCALLTYPE *o_EnumBySem)(IDirectInput8A *, LPCSTR, LPDIACTIONFORMATA, LPDIENUMDEVICESBYSEMANTICSCBA, LPVOID, DWORD);
static void *g_gameDI[8]; static int g_nGameDI;

static int is_game_di(void *p) { int i; for (i = 0; i < 8; i++) if (g_gameDI[i] == p) return 1; return 0; }
static int is_pad_type(DWORD t) { t &= 0xFF; return t >= DI8DEVTYPE_JOYSTICK && t <= DI8DEVTYPE_REMOTE; }

typedef struct { LPDIENUMDEVICESCALLBACKA cb; LPDIENUMDEVICESBYSEMANTICSCBA scb; LPVOID ref; int stopped, hidden, passed; } EnumCtx;
static BOOL CALLBACK filter_cb(LPCDIDEVICEINSTANCEA inst, LPVOID ref) {
    EnumCtx *c = ref;
    if (is_pad_type(inst->dwDevType)) { c->hidden++; return DIENUM_CONTINUE; }
    if (c->cb(inst, c->ref) == DIENUM_STOP) { c->stopped = 1; return DIENUM_STOP; }
    return DIENUM_CONTINUE;
}
// ---- input diagnostics (DiagInput=1): how often the game reads each device and what it gets ----
typedef struct DiagDev { void *dev; char name[24]; volatile LONG calls, ok, fail, events, acq, acqFail; volatile HRESULT lastErr; } DiagDev;
static DiagDev g_diag[12]; static volatile LONG g_ndiag;
static DiagDev *diag_find(void *dev) { LONG i; for (i = 0; i < g_ndiag; i++) if (g_diag[i].dev == dev) return &g_diag[i]; return NULL; }
static DiagDev *diag_add(void *dev, const char *name) {
    DiagDev *d = diag_find(dev);
    if (d || g_ndiag >= 12) return d;
    d = &g_diag[g_ndiag]; d->dev = dev; lstrcpynA(d->name, name, sizeof d->name);
    InterlockedIncrement(&g_ndiag);
    return d;
}
static HRESULT (STDMETHODCALLTYPE *o_RGetDeviceData)(IDirectInputDevice8A *, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
static HRESULT (STDMETHODCALLTYPE *o_RAcquire)(IDirectInputDevice8A *);
static volatile DWORD g_inputThread;      // thread the game reads input on (its main loop)
static volatile LONG g_totalReads;
static void diag_count(void *dev, HRESULT hr, DWORD n) {
    DiagDev *d = diag_find(dev);
    if (!d) return;
    g_inputThread = GetCurrentThreadId(); InterlockedIncrement(&g_totalReads);
    InterlockedIncrement(&d->calls);
    if (SUCCEEDED(hr)) { InterlockedIncrement(&d->ok); InterlockedExchangeAdd(&d->events, (LONG)n); }
    else { InterlockedIncrement(&d->fail); d->lastErr = hr; }
}
static HRESULT STDMETHODCALLTYPE h_RGetDeviceData(IDirectInputDevice8A *This, DWORD cb, LPDIDEVICEOBJECTDATA buf, LPDWORD inout, DWORD flags) {
    HRESULT hr = o_RGetDeviceData(This, cb, buf, inout, flags);
    diag_count(This, hr, inout ? *inout : 0);
    return hr;
}
static HRESULT STDMETHODCALLTYPE h_RAcquire(IDirectInputDevice8A *This) {
    HRESULT hr = o_RAcquire(This); DiagDev *d = diag_find(This);
    if (d) { InterlockedIncrement(&d->acq); if (FAILED(hr)) { InterlockedIncrement(&d->acqFail); d->lastErr = hr; } }
    return hr;
}
static int g_diagOn;
static void patch_slot(void **slot, void *hook, void **orig);
// Watchdog: when the game stops reading input, look at where its main thread is. The thread is
// paused only long enough to copy its registers and stack; names are looked up after it resumes.
static void describe_addr(DWORD a, char *out, int n) {
    HMODULE m = NULL; char path[MAX_PATH]; const char *b;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(UINT_PTR)a, &m) || !m) { snprintf(out, n, "%08lX", a); return; }
    GetModuleFileNameA(m, path, MAX_PATH); b = strrchr(path, '\\'); b = b ? b + 1 : path;
    if (m == GetModuleHandleA(NULL)) snprintf(out, n, "tcnyc+%06lX(%08lX)", a - (DWORD)(UINT_PTR)m, a);
    else snprintf(out, n, "%s+%lX", b, a - (DWORD)(UINT_PTR)m);
}
static int is_code_addr(DWORD a) {
    MEMORY_BASIC_INFORMATION mbi;
    if (a < 0x10000 || !VirtualQuery((void *)(UINT_PTR)a, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT || mbi.Type != MEM_IMAGE) return 0;
    return (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) != 0;
}
static void sample_main_thread(int k) {
    HANDLE t = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, g_inputThread);
    CONTEXT c; DWORD stack[1024]; SIZE_T got = 0; char line[1400], nm[96]; int len, i, hits = 0;
    if (!t) { logf_("watchdog: cannot open the game thread (error %lu)", GetLastError()); return; }
    memset(&c, 0, sizeof c); c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    if (SuspendThread(t) == (DWORD)-1) { CloseHandle(t); return; }
    GetThreadContext(t, &c);
    ReadProcessMemory(GetCurrentProcess(), (void *)(UINT_PTR)c.Esp, stack, sizeof stack, &got);
    ResumeThread(t); CloseHandle(t);
    describe_addr(c.Eip, nm, sizeof nm);
    len = snprintf(line, sizeof line, "watchdog sample %d: game thread at %s; stack:", k, nm);
    for (i = 0; i < (int)(got / 4) && hits < 20 && len < (int)sizeof line - 100; i++)
        if (is_code_addr(stack[i])) { describe_addr(stack[i], nm, sizeof nm); len += snprintf(line + len, sizeof line - len, " %s", nm); hits++; }
    logf_("%s", line);
}
static DWORD WINAPI DiagThread(LPVOID arg) {
    DWORD last = 0xFFFFFFFF, lastReport = GetTickCount(), lastSet = 0xFFFFFFFF, lastWnd = 0xFFFFFFFF; int lines = 0;
    (void)arg;
    for (;;) {
        // these game variables were worked out on one build; on any other they read as 0
        DWORD now = GetTickCount(), bits = g_knownBuild ? *(volatile DWORD *)0x0084A904 : 0;
        DWORD set = g_knownBuild ? *(volatile DWORD *)0x0075CCC0 : 0, wnd = g_knownBuild ? *(volatile DWORD *)0x00793380 : 0;
        DWORD mgr = g_knownBuild ? *(volatile DWORD *)0x0084A8D0 : 0;
        static DWORD lastMgr = 0xFFFFFFFF;
        if (mgr != lastMgr) { logf_("game control manager now %08lX", mgr); lastMgr = mgr; }
        if (bits != last && lines < 400) { logf_("game button state %08lX", bits); last = bits; lines++; }
        {   // watchdog
            static LONG seenReads; static DWORD lastReadAt; static int stalls, samples;
            LONG r = g_totalReads;
            if (r != seenReads) { if (stalls) logf_("watchdog: game is reading input again"); seenReads = r; lastReadAt = now; stalls = 0; }
            else if (r && g_inputThread && now - lastReadAt > 1500 && samples < 12 && stalls < 3) {
                if (!stalls) logf_("watchdog: game has not read input for %lu ms", now - lastReadAt);
                sample_main_thread(++samples); stalls++; lastReadAt = now - 1200;   // next sample in ~300 ms
            }
        }
        if (set != lastSet) { logf_("game control set now %lu (%s)", set, set < 3 ? g_modes[set].label : "?"); lastSet = set; }
        if (wnd != lastWnd) {
            char cls[64] = "", title[64] = "";
            if (wnd) { GetClassNameA((HWND)(UINT_PTR)wnd, cls, sizeof cls); GetWindowTextA((HWND)(UINT_PTR)wnd, title, sizeof title); }
            logf_("game input window now %08lX (class \"%s\", title \"%s\", exists %d)", wnd, cls, title, wnd ? IsWindow((HWND)(UINT_PTR)wnd) : 0);
            lastWnd = wnd;
        }
        if (now - lastReport >= 2000) {
            LONG i;
            for (i = 0; i < g_ndiag; i++) {
                DiagDev *d = &g_diag[i];
                logf_("diag %-22s reads %ld ok %ld fail %ld events %ld acquire %ld (failed %ld) last error %08lX",
                      d->name, InterlockedExchange(&d->calls, 0), InterlockedExchange(&d->ok, 0), InterlockedExchange(&d->fail, 0),
                      InterlockedExchange(&d->events, 0), InterlockedExchange(&d->acq, 0), InterlockedExchange(&d->acqFail, 0), (DWORD)d->lastErr);
            }
            logf_("diag round-robin index %lu of %lu, foreground is game: %d",
                  g_knownBuild ? *(volatile DWORD *)0x0084A940 : 0, g_knownBuild ? *(volatile DWORD *)0x0084A944 : 0, game_has_focus());
            lastReport = now;
        }
        Sleep(5);
    }
}
static BOOL CALLBACK filter_sem_cb(LPCDIDEVICEINSTANCEA inst, LPDIRECTINPUTDEVICE8A dev, DWORD fl, DWORD remaining, LPVOID ref) {
    EnumCtx *c = ref;
    if (is_pad_type(inst->dwDevType)) { c->hidden++; return DIENUM_CONTINUE; }
    c->passed++;
    if (g_diagOn && dev) {
        IDirectInputDevice8AVtbl *vt = (IDirectInputDevice8AVtbl *)dev->lpVtbl;
        diag_add(dev, inst->tszInstanceName);
        patch_slot((void **)&vt->GetDeviceData, (void *)h_RGetDeviceData, (void **)&o_RGetDeviceData);
        patch_slot((void **)&vt->Acquire, (void *)h_RAcquire, (void **)&o_RAcquire);
    }
    if (c->scb(inst, dev, fl, remaining + 1, c->ref) == DIENUM_STOP) { c->stopped = 1; return DIENUM_STOP; }
    return DIENUM_CONTINUE;
}
static HRESULT STDMETHODCALLTYPE h_CreateDevice(IDirectInput8A *self, REFGUID g, LPDIRECTINPUTDEVICE8A *out, LPUNKNOWN outer) {
    if (is_game_di(self) && g && out && IsEqualGUID(g, &GUID_PadInstance)) {
        Dev *d = dev_new();
        *out = d ? &d->iface : NULL;
        return d ? DI_OK : DIERR_OUTOFMEMORY;
    }
    return o_CreateDevice(self, g, out, outer);
}
static HRESULT STDMETHODCALLTYPE h_GetDeviceStatus(IDirectInput8A *self, REFGUID g) {
    if (g && IsEqualGUID(g, &GUID_PadInstance)) return DI_OK;
    return o_GetDeviceStatus(self, g);
}
static HRESULT STDMETHODCALLTYPE h_EnumDevices(IDirectInput8A *self, DWORD type, LPDIENUMDEVICESCALLBACKA cb, LPVOID ref, DWORD flags) {
    static int logs;
    EnumCtx c = {0}; HRESULT hr = DI_OK;
    if (!is_game_di(self) || !cb) return o_EnumDevices(self, type, cb, ref, flags);
    if (type != DI8DEVCLASS_ALL && type != DI8DEVCLASS_GAMECTRL && !is_pad_type(type)) return o_EnumDevices(self, type, cb, ref, flags);
    c.cb = cb; c.ref = ref;
    if (type == DI8DEVCLASS_ALL) hr = o_EnumDevices(self, type, filter_cb, &c, flags);
    if (!c.stopped && (!(flags & DIEDFL_FORCEFEEDBACK) || g_rumbleOn)) {
        DIDEVICEINSTANCEA di; fill_instance(&di, sizeof di);
        cb(&di, ref);
        if (logs++ < 4) logf_("Game looked for %scontrollers - offered the SDL3 controller", flags & DIEDFL_FORCEFEEDBACK ? "rumble " : "");
    }
    return hr;
}
static HRESULT STDMETHODCALLTYPE h_EnumBySem(IDirectInput8A *self, LPCSTR user, LPDIACTIONFORMATA f, LPDIENUMDEVICESBYSEMANTICSCBA cb, LPVOID ref, DWORD flags) {
    static int logs;
    EnumCtx c = {0}; HRESULT hr;
    if (!is_game_di(self) || !cb) return o_EnumBySem(self, user, f, cb, ref, flags);
    c.scb = cb; c.ref = ref;
    hr = o_EnumBySem(self, user, f, filter_sem_cb, &c, flags);
    if (!c.stopped) {
        Dev *d = dev_new();
        if (d) {
            DIDEVICEINSTANCEA di; fill_instance(&di, sizeof di);
            cb(&di, &d->iface, DIEDBS_MAPPEDPRI1, 0, ref);
            d_Release(&d->iface);   // the game keeps its own reference
        }
    }
    if (logs++ < 6) logf_("Game set up its controls (%s): DirectInput result %08lX, %d keyboard/mouse device(s), %d real controller(s) hidden, SDL3 controller added",
                          f ? (mode_of(f->dwGenre) >= 0 ? g_modes[mode_of(f->dwGenre)].label : "unknown set") : "?", hr, c.passed, c.hidden);
    return FAILED(hr) ? DI_OK : hr;
}
static void patch_slot(void **slot, void *hook, void **orig) {
    DWORD old;
    if (*slot == hook) return;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) { logf_("Could not patch DirectInput (error %lu)", GetLastError()); return; }
    *orig = *slot; *slot = hook;
    VirtualProtect(slot, sizeof(void *), old, &old);
}
static HRESULT WINAPI h_DI8Create(HINSTANCE inst, DWORD ver, REFIID iid, LPVOID *out, LPUNKNOWN outer) {
    static int logs;
    HRESULT hr = o_DI8Create(inst, ver, iid, out, outer);
    if (SUCCEEDED(hr) && out && *out && IsEqualIID(iid, &IID_IDirectInput8A)) {
        // DirectInput checks its own function table, so patch the slots in place (never swap the table).
        IDirectInput8AVtbl *vt = (IDirectInput8AVtbl *)((IDirectInput8A *)*out)->lpVtbl;
        g_gameDI[g_nGameDI++ & 7] = *out;
        patch_slot((void **)&vt->CreateDevice, (void *)h_CreateDevice, (void **)&o_CreateDevice);
        patch_slot((void **)&vt->EnumDevices, (void *)h_EnumDevices, (void **)&o_EnumDevices);
        patch_slot((void **)&vt->GetDeviceStatus, (void *)h_GetDeviceStatus, (void **)&o_GetDeviceStatus);
        patch_slot((void **)&vt->EnumDevicesBySemantics, (void *)h_EnumBySem, (void **)&o_EnumBySem);
        if (logs++ < 4) logf_("DirectInput created by the game - hooks in place");
        if (g_diagOn) {   // which game code path set input up: look for its known return addresses on the stack
            static const struct { DWORD ret; const char *what; } sites[] = {
                {0x0049BEDD, "game start-up"}, {0x00651C2E, "setup dialog opening"}, {0x006520FC, "reset-controls message box"},
                {0x0063DD90, "rumble device set-up"}, {0x00645D5B, "control manager set-up"},
            };
            DWORD *sp = (DWORD *)&hr, *top = sp + 4096; char line[300]; int len = 0; unsigned i;
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(sp, &mbi, sizeof mbi)) { DWORD *end = (DWORD *)((BYTE *)mbi.BaseAddress + mbi.RegionSize); if (top > end) top = end; }
            for (; sp < top && len < 260; sp++)
                for (i = 0; i < sizeof sites / sizeof sites[0]; i++)
                    if (*sp == sites[i].ret) len += snprintf(line + len, sizeof line - len, " [%s]", sites[i].what);
            line[len] = 0;
            logf_("diag DirectInput8Create called by:%s", len ? line : " (unknown path)");
        }
    } else if (logs++ < 4) logf_("DirectInput8Create: hr=%08lX, not the interface we handle", hr);
    return hr;
}

// ---------------------------------------------------------------------------------------------
// finding things in tcnyc.exe. Patches locate their code by byte pattern ("??" = any byte) instead
// of fixed addresses, and read the addresses they need out of the instructions they find, so other
// builds of the game work where the code is the same. A feature whose pattern is missing stays off.
// ---------------------------------------------------------------------------------------------
static BYTE *g_exeBase; static DWORD g_exeStamp;
static int g_knownBuild;   // the build every address in this file was worked out on (diagnostics only)
static int parse_pattern(const char *pat, BYTE *bytes, BYTE *mask) {
    int n = 0;
    while (*pat && n < 128) {
        while (*pat == ' ') pat++;
        if (!*pat) break;
        if (pat[0] == '?') { bytes[n] = 0; mask[n++] = 0; pat += pat[1] == '?' ? 2 : 1; continue; }
        bytes[n] = (BYTE)strtoul(pat, NULL, 16); mask[n++] = 1; pat += 2;
    }
    return n;
}
// all matches in the executable sections (code = 1) or in every section (code = 0); returns the count
static int find_pattern(const char *pat, int code, BYTE **hits, int maxHits) {
    BYTE b[128], m[128]; int n = parse_pattern(pat, b, m), found = 0, i;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(g_exeBase + ((IMAGE_DOS_HEADER *)g_exeBase)->e_lfanew);
    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        BYTE *p, *end;
        if (code && !(sec->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        p = g_exeBase + sec->VirtualAddress; end = p + sec->Misc.VirtualSize;
        if (end - p < n || IsBadReadPtr(p, end - p)) continue;
        for (end -= n; p <= end; p++) {
            int k;
            if (*p != b[0]) continue;
            for (k = 1; k < n && (!m[k] || p[k] == b[k]); k++) ;
            if (k == n) { if (found < maxHits) hits[found] = p; found++; }
        }
    }
    return found;
}
static BYTE *find_one(const char *pat, int code) { BYTE *h[2]; return find_pattern(pat, code, h, 2) == 1 ? h[0] : NULL; }
static void exe_identify(void) {
    IMAGE_NT_HEADERS *nt;
    g_exeBase = (BYTE *)GetModuleHandleA(NULL);
    nt = (IMAGE_NT_HEADERS *)(g_exeBase + ((IMAGE_DOS_HEADER *)g_exeBase)->e_lfanew);
    g_exeStamp = nt->FileHeader.TimeDateStamp;
    g_knownBuild = g_exeStamp == 0x4410A579 && nt->OptionalHeader.SizeOfImage == 0x14E7000 && g_exeBase == (BYTE *)0x400000;
    logf_("tcnyc.exe build %08lX%s", g_exeStamp, g_knownBuild ? " (the build this plugin was developed on)" : " (another build: features are located by pattern)");
}

// ---------------------------------------------------------------------------------------------
// button prompts. The game still builds its hints the console way ("press <Xbox button>"); the PC
// port only turns that button into the name of the default keyboard key at the very end
// (0x63ED90). That last step is replaced: the button becomes the game action, the action becomes
// whatever controller button the ini puts it on, and that button's name is shown.
// ---------------------------------------------------------------------------------------------
static BYTE *g_vaPromptName;            // const char *__cdecl (int consoleButtonMask)     (0x63ED90 here)
static BYTE *g_vaMask2Action;           // int __cdecl (int consoleButtonMask) -> action   (0x63EAF0 here)
static volatile int *g_vaCurSet;        // current control set: 0 on foot, 1 driving, 2 menus (0x75CCC0 here)
static int g_promptMode = 1, g_promptNames = 0, g_promptsOn;
static const char *(__cdecl *o_PromptName)(int);

static const char *button_name(int btn, int ps) {
    static const char *xb[NBTN] = {"A", "B", "X", "Y", "Back", "Guide", "Start", "LS", "RS", "LB", "RB",
        "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "Share", "P1", "P2", "P3", "P4", "Touchpad",
        "Misc", "Misc", "Misc", "Misc", "Misc", "LT", "RT"};
    static const char *pl[NBTN] = {"Cross", "Circle", "Square", "Triangle", "Create", "PS", "Options", "L3", "R3", "L1", "R1",
        "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right", "Mute", "P1", "P2", "P3", "P4", "Touchpad",
        "Misc", "Misc", "Misc", "Misc", "Misc", "L2", "R2"};
    return btn >= 0 && btn < NBTN ? (ps ? pl : xb)[btn] : NULL;
}
static const char *__cdecl h_PromptName(int mask) {
    static struct { int mask, set; } seen[64]; static int nseen;
    int set = *g_vaCurSet, app, src, ps, i;
    const char *r = NULL;
    if (g_promptMode == 2 || (g_promptMode == 1 && g_snap.connected)) {
        app = ((int (__cdecl *)(int))g_vaMask2Action)(mask);
        if (set < 0 || set > 2) set = 0;
        src = app > 0 && app < 256 ? g_map[set][app] : SRC_NONE;
        ps = g_promptNames == 2 || (g_promptNames == 0 && g_psPad);
        if (SRC_KIND(src) == 0x200) r = button_name(SRC_IDX(src), ps);
        else if (app >= 0x19 && app <= 0x1C)   // d-pad directions: the d-pad always drives these
            r = button_name(SDL_GAMEPAD_BUTTON_DPAD_UP + (app - 0x19), ps);
    }
    if (!r) r = o_PromptName(mask);
    for (i = 0; i < nseen && (seen[i].mask != mask || seen[i].set != set); i++) ;
    if (i == nseen && nseen < 64) {
        seen[nseen].mask = mask; seen[nseen++].set = set;
        logf_("Prompt: console button %04X in %s -> \"%s\"", mask, g_modes[set >= 0 && set <= 2 ? set : 0].label, r ? r : "(null)");
    }
    return r;
}
static void install_prompts(void) {
    // mov eax,[curSet] / mov edx,[esp+4] / lea / lea / add / push esi / push edi / add / mov edi,[..] / add / push edx / call mask2action
    static const char *sig = "A1 ?? ?? ?? ?? 8B 54 24 04 8D 0C 80 8D 0C C8 03 C9 56 57 03 C9 8B BC 09 ?? ?? ?? ?? 03 C9 52 E8";
    static const unsigned char m2a[9] = {0x8B, 0x44, 0x24, 0x04, 0x3D, 0x00, 0x01, 0x00, 0x00};   // mov eax,[esp+4]; cmp eax,100h
    BYTE *p, *t; DWORD old;
    if (!g_promptMode) { logf_("Button prompts: off (ButtonPrompts=0)"); return; }
    if (!(p = find_one(sig, 1))) { logf_("Button prompts: NOT installed, the game's key-name code was not found in this tcnyc.exe"); return; }
    g_vaPromptName = p;
    g_vaCurSet = *(volatile int **)(p + 1);
    g_vaMask2Action = p + 36 + *(int *)(p + 32);
    if (IsBadReadPtr(g_vaMask2Action, 9) || memcmp(g_vaMask2Action, m2a, 9) || IsBadReadPtr((void *)g_vaCurSet, 4)) {
        logf_("Button prompts: NOT installed, the code around the game's key names is not the expected shape"); return;
    }
    // trampoline for the original: its first instruction (an absolute load, safe to move), then back into the function
    t = VirtualAlloc(NULL, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!t) { logf_("Button prompts: NOT installed (no memory)"); return; }
    memcpy(t, p, 5); t[5] = 0xE9; *(int *)(t + 6) = (int)(UINT_PTR)(p + 5) - (int)(UINT_PTR)(t + 10);
    o_PromptName = (const char *(__cdecl *)(int))t;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) { logf_("Button prompts: NOT installed (error %lu)", GetLastError()); return; }
    p[0] = 0xE9; *(int *)(p + 1) = (int)(UINT_PTR)h_PromptName - (int)(UINT_PTR)(p + 5);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    g_promptsOn = 1;
    logf_("Button prompts: installed at %p (ButtonPrompts=%d, ButtonNames=%s)", (void *)p, g_promptMode,
          g_promptNames == 1 ? "xbox" : g_promptNames == 2 ? "playstation" : "auto");
}

static int hook_import(const char *dll, const char *fn, void *hook, void **orig);
static int prompts_active(void) { return g_promptsOn && (g_promptMode == 2 || (g_promptMode == 1 && g_snap.connected)); }
static int prompts_ps(void) { return g_promptNames == 2 || (g_promptNames == 0 && g_psPad); }

// ---------------------------------------------------------------------------------------------
// hard-coded key names in the game's text. Nearly all prompts use $INPUT_...$ tokens (handled
// above), but two strings in LangTable.dat name keys literally. The table is caught as the game
// reads it from disk and those strings are rewritten in place (never longer than the original).
// ---------------------------------------------------------------------------------------------
typedef struct { char *p; int len; char orig[256]; char now[256]; } TextSite;
static TextSite g_texts[16]; static int g_ntexts, g_textPad = -1;
static int g_nLangReads; static void lang_reads_poll(void);
static HANDLE g_langFiles[8];
static HANDLE (WINAPI *o_CreateFileA)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static BOOL (WINAPI *o_ReadFile)(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED);

static void text_wanted(const TextSite *t, int pad, char *out) {
    memcpy(out, t->orig, t->len + 1);
    if (!pad) return;
    if (!strcmp(t->orig, "Press ENTER")) {         // the menus' Select button
        int src = g_map[2][0x24]; const char *nm = SRC_KIND(src) == 0x200 ? button_name(SRC_IDX(src), prompts_ps()) : NULL;
        if (nm && (int)strlen(nm) + 6 <= t->len) snprintf(out, t->len + 1, "Press %s", nm);
    } else {                                       // "... or B to free more blocks." is an Xbox leftover
        char *k = strstr(out, " or B to free more blocks.");
        if (k) memcpy(k, ".", 2);
    }
}
static void text_apply(int mode) {   // 0 = keyboard wording, 1 = Xbox names, 2 = PlayStation names
    int i, pad = mode != 0;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < g_ntexts; i++) {
        TextSite *t = &g_texts[i]; char want[256];
        if (!t->p) continue;
        if (IsBadWritePtr(t->p, t->len + 1) || memcmp(t->p, t->now, t->len + 1)) { t->p = NULL; continue; }   // buffer gone or reused
        text_wanted(t, pad, want);
        if (memcmp(want, t->now, t->len + 1)) { memcpy(t->p, want, t->len + 1); memcpy(t->now, want, t->len + 1); }
    }
    g_textPad = mode;
    LeaveCriticalSection(&g_cs);
}
static int text_mode(void) { return prompts_active() ? 1 + prompts_ps() : 0; }
static void text_scan(char *buf, DWORD n) {
    static const char *targets[] = {"Press ENTER", "or B to free more blocks."};
    unsigned k; int found = 0;
    EnterCriticalSection(&g_cs);   // re-entrant; keeps the worker's text_apply out while sites are added
    for (k = 0; k < sizeof targets / sizeof targets[0]; k++) {
        size_t tl = strlen(targets[k]); char *p = buf, *end = buf + n;
        while (p < end && (p = memchr(p, targets[k][0], end - p)) && p + tl < end) {
            if (!memcmp(p, targets[k], tl)) {
                char *s = p; int len;
                while (s > buf && s[-1]) s--;                     // start of this string
                len = (int)strnlen(s, end - s);
                if (s + len < end && len < 255 && g_ntexts < 16 && (k != 0 || len == (int)tl)) {
                    TextSite *t = &g_texts[g_ntexts++];
                    t->p = s; t->len = len; memcpy(t->orig, s, len + 1); memcpy(t->now, s, len + 1);
                    found++;
                    logf_("Game text: found \"%.60s%s\"", s, len > 60 ? "..." : "");
                }
                p = s + len;
            } else p++;
        }
    }
    if (found) { logf_("Game text: %d hard-coded key name(s) found, will show controller buttons", found); text_apply(text_mode()); }
    LeaveCriticalSection(&g_cs);
}
static void text_update(void) {   // worker thread: follow controller connect/disconnect and pad type
    if (g_nLangReads) lang_reads_poll();
    if (g_ntexts && text_mode() != g_textPad) text_apply(text_mode());
}
static HANDLE WINAPI h_CreateFileA(LPCSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD fl, HANDLE tmpl) {
    HANDLE h = o_CreateFileA(name, acc, share, sa, disp, fl, tmpl);
    int i, isLang;
    if (h == INVALID_HANDLE_VALUE) return h;
    isLang = name && strlen(name) >= 13 && !_stricmp(name + strlen(name) - 13, "LangTable.dat");
    EnterCriticalSection(&g_cs);
    for (i = 0; i < 8; i++) if (g_langFiles[i] == h) g_langFiles[i] = NULL;   // handle values get reused
    if (isLang) for (i = 0; i < 8; i++) if (!g_langFiles[i]) { g_langFiles[i] = h; break; }
    LeaveCriticalSection(&g_cs);
    return h;
}
// The game reads the table with overlapped (asynchronous) I/O, so the data is not there yet when the
// read call returns. Remember where it is going and let the worker scan it a moment later.
static struct { char *buf; DWORD n, at; } g_langReads[16]; static int g_nLangReads;
static BOOL (WINAPI *o_ReadFileEx)(HANDLE, LPVOID, DWORD, LPOVERLAPPED, LPOVERLAPPED_COMPLETION_ROUTINE);
static void note_lang_read(HANDLE h, LPVOID buf, DWORD n) {
    int i, lang = 0;
    if (!buf || !n) return;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < 8; i++) if (g_langFiles[i] == h) lang = 1;
    if (lang && g_nLangReads < 16) { g_langReads[g_nLangReads].buf = buf; g_langReads[g_nLangReads].n = n; g_langReads[g_nLangReads].at = GetTickCount(); g_nLangReads++; }
    LeaveCriticalSection(&g_cs);
}
static BOOL WINAPI h_ReadFile(HANDLE h, LPVOID buf, DWORD n, LPDWORD got, LPOVERLAPPED ov) {
    note_lang_read(h, buf, n);
    return o_ReadFile(h, buf, n, got, ov);
}
static BOOL WINAPI h_ReadFileEx(HANDLE h, LPVOID buf, DWORD n, LPOVERLAPPED ov, LPOVERLAPPED_COMPLETION_ROUTINE cr) {
    note_lang_read(h, buf, n);
    return o_ReadFileEx(h, buf, n, ov, cr);
}
static void lang_reads_poll(void) {   // worker thread
    int i, j;
    EnterCriticalSection(&g_cs);
    for (i = 0; i < g_nLangReads; ) {
        if (GetTickCount() - g_langReads[i].at < 500) { i++; continue; }
        if (g_diagOn) logf_("diag text table read: %lu bytes at %p", g_langReads[i].n, (void *)g_langReads[i].buf);
        if (!IsBadReadPtr(g_langReads[i].buf, g_langReads[i].n)) text_scan(g_langReads[i].buf, g_langReads[i].n);
        for (j = i + 1; j < g_nLangReads; j++) g_langReads[j - 1] = g_langReads[j];
        g_nLangReads--;
    }
    LeaveCriticalSection(&g_cs);
}

// ---------------------------------------------------------------------------------------------
// the quit screen. It is a picture (Data\Shell\QuitGame.pct, "Y/N" painted in) and it reads the
// Y and N keys straight from Windows (GetAsyncKeyState at 0x4A8D57 / 0x4BE3E4) while the prompt
// flag 0x793359 is set. With a controller in use: A/Cross answers Y, B/Circle answers N, and the
// game is handed a copy of the picture that names those buttons. The copy is drawn here, from the
// player's own game file, so no game artwork is shipped with the plugin.
// ---------------------------------------------------------------------------------------------
static volatile BYTE *g_vaQuitFlag;     // quit prompt open (0x793359 here)
static BYTE *g_vaQuitCall;              // the call that loads its picture (0x648D0D here)
static void *g_vaLoadPicture;           // picture loader (path, 1, 0) (0x62B4D0 here)
static int g_quitOn = 1, g_quitKeysOk, g_quitPicOk;
static SHORT (WINAPI *o_GetAsyncKeyState)(int);
static char g_quitPic[2][MAX_PATH];      // generated pictures: [0] Xbox names, [1] PlayStation names

static int make_quit_picture(int ps, const char *out) {
    char src[MAX_PATH], *s; HANDLE f; DWORD size, got; BYTE *data; int ok = 0;
    GetModuleFileNameA(NULL, src, MAX_PATH); s = strrchr(src, '\\'); s = s ? s + 1 : src;
    snprintf(s, MAX_PATH - (s - src), "Data\\Shell\\QuitGame.pct");
    f = CreateFileA(src, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) { logf_("Quit screen picture: %s not found", src); return 0; }
    size = GetFileSize(f, NULL);
    data = HeapAlloc(GetProcessHeap(), 0, size ? size : 1);
    if (data && ReadFile(f, data, size, &got, NULL) && got == size && size == 0x80 + 640 * 448 * 4 &&
        ((DWORD *)data)[0] == 7 && ((DWORD *)data)[1] == ((448u << 16) | 640u) && ((DWORD *)data)[3] == 0x80 && ((DWORD *)data)[6] == 640 * 448 * 4) {
        BITMAPINFO bi; void *bits; HDC dc = CreateCompatibleDC(NULL); HBITMAP bm; HFONT font, oldf;
        memset(&bi, 0, sizeof bi);
        bi.bmiHeader.biSize = sizeof bi.bmiHeader; bi.bmiHeader.biWidth = 640; bi.bmiHeader.biHeight = -448;
        bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
        bm = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (bm && bits) {
            const char *label = ps ? "Cross: Yes     Circle: No" : "A: Yes     B: No";
            DWORD *px = bits, *orig = (DWORD *)(data + 0x80); int i, x, y, x0 = 640, y0 = 448, x1 = -1, y1 = -1;
            HGDIOBJ oldb = SelectObject(dc, bm);
            // draw the label on black, find where its pixels actually are, then place them where "Y/N" was
            memset(bits, 0, 640 * 448 * 4);
            font = CreateFontA(-29, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_SWISS, "Trebuchet MS");   // closest match to the game's lettering
            oldf = SelectObject(dc, font);
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
            TextOutA(dc, 20, 20, label, (int)strlen(label));
            GdiFlush();
            for (y = 0; y < 120; y++) for (x = 0; x < 640; x++)
                if ((px[y * 640 + x] & 0xFF) > 96) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
            if (x1 >= 0) {
                int dx = 324 - (x0 + x1) / 2, dy = 253 - y0;              // centre on the old text, top of capitals at y=253
                for (y = 247; y < 282; y++) for (x = 286; x < 362; x++) orig[y * 640 + x] = 0xFF000000u;   // remove "Y/N"
                for (y = y0 - 4; y <= y1 + 4; y++) for (x = x0 - 2; x <= x1 + 2; x++) {
                    int tx = x + dx, ty = y + dy; DWORD v;
                    if (y < 0 || x < 0 || tx < 0 || tx >= 640 || ty < 0 || ty >= 448) continue;
                    v = px[y * 640 + x] & 0xFF;
                    if (v > (orig[ty * 640 + tx] & 0xFF)) orig[ty * 640 + tx] = 0xFF000000u | v << 16 | v << 8 | v;
                }
            }
            for (i = 0; i < 640 * 448; i++) orig[i] |= 0xFF000000u;
            SelectObject(dc, oldf); DeleteObject(font); SelectObject(dc, oldb);
            {
                HANDLE o = CreateFileA(out, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); DWORD wr = 0;
                if (o != INVALID_HANDLE_VALUE) { ok = WriteFile(o, data, size, &wr, NULL) && wr == size; CloseHandle(o); }
            }
        }
        if (bm) DeleteObject(bm);
        DeleteDC(dc);
    } else logf_("Quit screen picture: %s is not the expected format", src);
    if (data) HeapFree(GetProcessHeap(), 0, data);
    CloseHandle(f);
    logf_("Quit screen picture (%s names): %s", ps ? "PlayStation" : "Xbox", ok ? "created" : "could NOT be created");
    return ok;
}
static unsigned __cdecl h_LoadQuitPicture(const char *path, int a, int b) {
    unsigned (__cdecl *load)(const char *, int, int) = (unsigned (__cdecl *)(const char *, int, int))g_vaLoadPicture;
    if (prompts_active()) {
        int ps = prompts_ps();
        if (GetFileAttributesA(g_quitPic[ps]) == INVALID_FILE_ATTRIBUTES) make_quit_picture(ps, g_quitPic[ps]);
        if (GetFileAttributesA(g_quitPic[ps]) != INVALID_FILE_ATTRIBUTES) {
            unsigned r = load(g_quitPic[ps], a, b);
            if (r > 1) return r;
            logf_("Quit screen: the game could not load %s - showing its own picture", g_quitPic[ps]);
        }
    }
    return load(path, a, b);   // a failed load here would make the game quit straight away, so always fall back
}
static SHORT WINAPI h_GetAsyncKeyState(int vk) {
    SHORT r = o_GetAsyncKeyState(vk);
    static DWORD lastCall; static uint32_t base[2];
    if ((vk == 'Y' || vk == 'N') && g_quitKeysOk && *g_vaQuitFlag) {
        PadSnap s; DWORD now = GetTickCount(); int k = vk == 'N', btn = k ? SDL_GAMEPAD_BUTTON_EAST : SDL_GAMEPAD_BUTTON_SOUTH;
        get_snap(&s);
        if (now - lastCall > 300) {   // prompt just opened: only presses made from now on count
            base[0] = s.presses[SDL_GAMEPAD_BUTTON_SOUTH]; base[1] = s.presses[SDL_GAMEPAD_BUTTON_EAST];
        }
        if (vk == 'N') lastCall = now;
        if (s.live && (s.buttons >> btn & 1) && s.presses[btn] != base[k]) {
            static int logged;
            if (logged++ < 4) logf_("Quit screen: %s pressed on the controller", k ? "No" : "Yes");
            r |= (SHORT)(-32767 - 1);   // "key is down"
        }
    }
    return r;
}
static void install_quit_screen(void) {
    // cmp byte [quitFlag],0 / je / mov esi,[GetAsyncKeyState] / push 'Y' / call esi / test / jns / mov byte [..],1 / push 'N' / call esi
    static const char *keySig = "80 3D ?? ?? ?? ?? 00 74 ?? 8B 35 ?? ?? ?? ?? 6A 59 FF D6 84 E4 79 ?? C6 05 ?? ?? ?? ?? 01 6A 4E FF D6";
    BYTE *hits[4], *str, *c; DWORD old; char *s, sig[96]; int n, i;
    if (!g_quitOn) { logf_("Quit screen buttons: off (QuitScreenButtons=0)"); return; }
    n = find_pattern(keySig, 1, hits, 4);
    for (i = 1; i < n && i < 4; i++) if (*(DWORD *)(hits[i] + 2) != *(DWORD *)(hits[0] + 2)) n = 0;   // every copy must use the same flag
    if (n < 1 || n > 4) { logf_("Quit screen buttons: NOT installed, the quit prompt's key checks were not found in this tcnyc.exe"); return; }
    g_vaQuitFlag = *(volatile BYTE **)(hits[0] + 2);
    g_quitKeysOk = hook_import("USER32.dll", "GetAsyncKeyState", (void *)h_GetAsyncKeyState, (void **)&o_GetAsyncKeyState);
    snprintf(g_quitPic[0], MAX_PATH, "%sTCNYCSDL3Pad", g_dir); CreateDirectoryA(g_quitPic[0], NULL);
    s = g_quitPic[0] + strlen(g_quitPic[0]);
    snprintf(g_quitPic[1], MAX_PATH, "%s\\QuitGame_PlayStation.pct", g_quitPic[0]);
    snprintf(s, MAX_PATH - (s - g_quitPic[0]), "\\QuitGame_Xbox.pct");
    // the picture: find its file name, then the "push <name> / mov dword [..],1 / call loader" that uses it
    if ((str = find_one("21 53 48 45 4C 4C 21 5C 51 75 69 74 47 61 6D 65 2E 70 63 74 00", 0))) {   /* "!SHELL!\QuitGame.pct" */
        DWORD a = (DWORD)(UINT_PTR)str;
        snprintf(sig, sizeof sig, "68 %02X %02X %02X %02X C7 05 ?? ?? ?? ?? 01 00 00 00 E8", a & 0xFF, a >> 8 & 0xFF, a >> 16 & 0xFF, a >> 24);
        if ((c = find_one(sig, 1))) {
            c += 15;
            g_vaQuitCall = c; g_vaLoadPicture = c + 5 + *(int *)(c + 1);
            if (VirtualProtect(c, 5, PAGE_EXECUTE_READWRITE, &old)) {
                *(int *)(c + 1) = (int)((UINT_PTR)h_LoadQuitPicture - (UINT_PTR)(c + 5));
                VirtualProtect(c, 5, old, &old); FlushInstructionCache(GetCurrentProcess(), c, 5);
                g_quitPicOk = 1;
            }
        }
    }
    logf_("Quit screen buttons: %s (%d key check%s), picture: %s", g_quitKeysOk ? "installed" : "FAILED", n, n == 1 ? "" : "s",
          g_quitPicOk ? "installed" : "NOT installed (its loader was not found)");
}
static void quit_pictures_prepare(void) {   // worker thread, once a controller is in use
    int ps;
    if (!g_quitPicOk) return;
    for (ps = 0; ps < 2; ps++) if (GetFileAttributesA(g_quitPic[ps]) == INVALID_FILE_ATTRIBUTES) make_quit_picture(ps, g_quitPic[ps]);
}
static void install_text(void) {
    int a = hook_import("KERNEL32.dll", "CreateFileA", (void *)h_CreateFileA, (void **)&o_CreateFileA);
    int b = a && hook_import("KERNEL32.dll", "ReadFile", (void *)h_ReadFile, (void **)&o_ReadFile);
    if (b) hook_import("KERNEL32.dll", "ReadFileEx", (void *)h_ReadFileEx, (void **)&o_ReadFileEx);
    logf_("Hard-coded key names in text: %s", b ? "watching for the game's text table" : "NOT installed");
}

// ---------------------------------------------------------------------------------------------
// startup
// ---------------------------------------------------------------------------------------------
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
        for (; names->u1.AddressOfData; names++, iat++) {
            if (names->u1.Ordinal & IMAGE_ORDINAL_FLAG) continue;
            if (!strcmp((char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name, fn)) return (void **)&iat->u1.Function;
        }
    }
    return NULL;
}
static int hook_import(const char *dll, const char *fn, void *hook, void **orig) {
    void **slot = find_import(dll, fn); DWORD old;
    if (!slot || !VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return 0;
    *orig = *slot; *slot = hook;
    VirtualProtect(slot, sizeof(void *), old, &old);
    return 1;
}
static float ini_pct(const char *key, int def, int lo, int hi) {
    int v = GetPrivateProfileIntA("Settings", key, def, g_ini);
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    return v / 100.f;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        char *s; int m, i;
        DisableThreadLibraryCalls(h); InitializeCriticalSection(&g_cs);
        GetModuleFileNameA(h, g_dir, MAX_PATH); s = strrchr(g_dir, '\\'); if (s) s[1] = 0;
        snprintf(g_log, MAX_PATH, "%sTCNYCSDL3Pad.log", g_dir);
        snprintf(g_ini, MAX_PATH, "%sTCNYCSDL3Pad.ini", g_dir);
        DeleteFileA(g_log);
        snprintf(g_logTag, sizeof g_logTag, "(pid %lu, module %p) ", GetCurrentProcessId(), (void *)h);
        g_enabled     = GetPrivateProfileIntA("Settings", "Enabled", 1, g_ini);
        g_rumbleOn    = GetPrivateProfileIntA("Settings", "Rumble", 1, g_ini);
        g_rumbleScale = ini_pct("RumbleStrength", 100, 0, 200);
        g_deadzone    = ini_pct("StickDeadzone", 15, 0, 90);
        g_trigThreshold = ini_pct("TriggerThreshold", 30, 1, 100);
        g_invertAimY  = GetPrivateProfileIntA("Settings", "InvertAimY", 0, g_ini);
        g_swapSticks  = GetPrivateProfileIntA("Settings", "SwapSticks", 0, g_ini);
        g_bgInput     = GetPrivateProfileIntA("Settings", "InputInBackground", 0, g_ini);
        g_logInput    = GetPrivateProfileIntA("Settings", "LogInput", 0, g_ini);
        g_diagOn      = GetPrivateProfileIntA("Settings", "DiagInput", 0, g_ini);
        g_cancelGameDz = GetPrivateProfileIntA("Settings", "CancelGameDeadzone", 1, g_ini);
        g_quitOn      = GetPrivateProfileIntA("Settings", "QuitScreenButtons", 1, g_ini);
        g_promptMode  = GetPrivateProfileIntA("Settings", "ButtonPrompts", 1, g_ini);
        {
            char v[32];
            GetPrivateProfileStringA("Settings", "ButtonNames", "auto", v, sizeof v, g_ini);
            g_promptNames = !_strnicmp(v, "x", 1) ? 1 : !_strnicmp(v, "p", 1) ? 2 : 0;
        }
        logf_("TCNYCSDL3Pad v" VERSION " loaded. Rumble=%d StickDeadzone=%d%% CancelGameDeadzone=%d TriggerThreshold=%d%% InvertAimY=%d SwapSticks=%d",
              g_rumbleOn, (int)(g_deadzone * 100 + .5f), g_cancelGameDz, (int)(g_trigThreshold * 100 + .5f), g_invertAimY, g_swapSticks);
        if (!g_enabled) { logf_("Enabled=0 - doing nothing"); return TRUE; }
        exe_identify();
        load_maps();
        for (m = 0; m < 3; m++) {
            char line[512]; int n = 0;
            for (i = 0; i < g_modes[m].n; i++) {
                const ActDef *a = &g_modes[m].acts[i]; int src = g_map[m][a->app], j; const char *nm = "?";
                if (!src) continue;
                for (j = 0; j < (int)(sizeof g_btnNames / sizeof g_btnNames[0]); j++) if (SRC_BTN(g_btnNames[j].btn) == src) { nm = g_btnNames[j].name; break; }
                n += snprintf(line + n, sizeof line - n, "%s%s=%s", n ? ", " : "", a->key, nm);
                if (n >= (int)sizeof line) break;
            }
            logf_("Layout %s: %s", g_modes[m].label, line);
        }
        if (!hook_import("DINPUT8.dll", "DirectInput8Create", (void *)h_DI8Create, (void **)&o_DI8Create))
            logf_("DirectInput hook: NOT installed (tcnyc.exe has no DINPUT8 import?) - plugin inactive");
        else {
            logf_("DirectInput hook: installed");
            install_prompts();
            if (g_promptsOn) { install_text(); install_quit_screen(); }
            CreateThread(NULL, 0, Worker, NULL, 0, NULL);
            if (g_diagOn) { logf_("Input diagnostics on (DiagInput=1)"); CreateThread(NULL, 0, DiagThread, NULL, 0, NULL); }
        }
    }
    return TRUE;
}
