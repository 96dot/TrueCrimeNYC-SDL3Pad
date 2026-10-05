// The small slice of the SDL3 API this plugin uses, loaded from SDL3.dll at runtime.
// Types and values match the SDL 3.2 headers (SDL's ABI is stable across 3.x).
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef uint32_t SDL_JoystickID;
typedef struct SDL_Gamepad SDL_Gamepad;

#define SDL_INIT_GAMEPAD            0x00002000u
#define SDL_EVENT_GAMEPAD_ADDED     0x653
#define SDL_EVENT_GAMEPAD_REMOVED   0x654

typedef union SDL_Event {
    uint32_t type;
    struct { uint32_t type, reserved; uint64_t timestamp; SDL_JoystickID which; } gdevice;
    uint8_t padding[128];
} SDL_Event;

enum {  // SDL_GamepadButton
    SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_GUIDE, SDL_GAMEPAD_BUTTON_START,
    SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    SDL_GAMEPAD_BUTTON_MISC1,
    SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1,
    SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2,
    SDL_GAMEPAD_BUTTON_TOUCHPAD,
    SDL_GAMEPAD_BUTTON_COUNT = 26
};
enum {  // SDL_GamepadAxis
    SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY,
    SDL_GAMEPAD_AXIS_LEFT_TRIGGER, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, SDL_GAMEPAD_AXIS_COUNT
};
enum { SDL_GAMEPAD_TYPE_PS3 = 4, SDL_GAMEPAD_TYPE_PS4 = 5, SDL_GAMEPAD_TYPE_PS5 = 6 };

#define SDL_FUNCS(X) \
    X(bool,          SDL_SetHint,                 (const char *, const char *)) \
    X(bool,          SDL_Init,                    (uint32_t)) \
    X(const char *,  SDL_GetError,                (void)) \
    X(int,           SDL_GetVersion,              (void)) \
    X(bool,          SDL_PollEvent,               (SDL_Event *)) \
    X(SDL_Gamepad *, SDL_OpenGamepad,             (SDL_JoystickID)) \
    X(void,          SDL_CloseGamepad,            (SDL_Gamepad *)) \
    X(bool,          SDL_GetGamepadButton,        (SDL_Gamepad *, int)) \
    X(int16_t,       SDL_GetGamepadAxis,          (SDL_Gamepad *, int)) \
    X(const char *,  SDL_GetGamepadName,          (SDL_Gamepad *)) \
    X(int,           SDL_GetGamepadType,          (SDL_Gamepad *)) \
    X(const char *,  SDL_GetGamepadStringForType, (int)) \
    X(uint16_t,      SDL_GetGamepadVendor,        (SDL_Gamepad *)) \
    X(uint16_t,      SDL_GetGamepadProduct,       (SDL_Gamepad *)) \
    X(bool,          SDL_RumbleGamepad,           (SDL_Gamepad *, uint16_t, uint16_t, uint32_t)) \
    X(void,          SDL_Delay,                   (uint32_t)) \
    X(void,          SDL_SetMainReady,            (void))
