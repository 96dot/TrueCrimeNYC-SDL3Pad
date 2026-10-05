# TCNYCSDL3Pad

Proper controller support for **True Crime: New York City** (PC, 2006), through SDL3.

- Correct controls on modern controllers (Xbox, PlayStation, Switch and most others): right-stick
  aiming, Target Lock and Fire on the triggers (and usable together), d-pad weapon selection.
- Rumble.
- On-screen hints show controller buttons ("Press Cross to Save Game") and follow your remapping.
- Every action remappable per mode, including actions the PC version only had on the keyboard.
- Hot-plug, no stuck or lost buttons.

See [CHANGELOG.md](CHANGELOG.md) for details of every version.

## Requirements

- True Crime: New York City, PC. Developed against `tcnyc.exe` 20,135,936 bytes,
  MD5 `b7eee2f3f4c2014d235acf238716b495`. Controller input works on any build that uses the stock
  DirectInput code; button prompts are only enabled on this exact exe (checked at runtime).
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (as `dinput8.dll`),
  for example from ThirteenAG's widescreen fix.
- `SDL3.dll`, **32-bit (x86)**, version 3.2 or newer, from the
  [SDL releases](https://github.com/libsdl-org/SDL/releases) (`SDL3-3.x.x-win32-x86.zip`).

## Install

1. Put `TCNYCSDL3Pad.asi` and `TCNYCSDL3Pad.ini` in the game's `scripts\` folder.
2. Put `SDL3.dll` next to `tcnyc.exe`.
3. Start the game. `scripts\TCNYCSDL3Pad.log` is written on every launch.

To remove: delete those three files.

## Default controls

The game was designed for the original Xbox controller; its Black and White buttons are LB and RB.

| Button | On foot | Driving | Menus |
|---|---|---|---|
| Left stick | Move | Steer, gas/brake | Cursor |
| Right stick | Aim | Aim / look | Cursor |
| D-pad | Weapon / combat mode | Weapon / song | Navigate |
| A / Cross | Light attack | Accelerate | Select |
| B / Circle | Grapple | Handbrake | Back / clear waypoint |
| X / Square | Heavy attack | Brake / reverse | Set waypoint |
| Y / Triangle | Jump | Look behind | Y |
| LB / L1 | Use / action | Car door | |
| RB / R1 | Reload / block | Reload | |
| LT / L2 | Target lock | Target lock | Left trigger |
| RT / R2 | Fire | Fire | Right trigger |
| Back / Create | Badge / warning shot | Siren / horn | Back |
| Start / Options | Pause / map | Pause / map | Exit |
| LS / L3 | Crouch | | |
| RS / R3 | Precision aim | Precision aim | |

## Settings (`TCNYCSDL3Pad.ini`)

| Setting | Default | Meaning |
|---|---|---|
| `Enabled` | 1 | 0 turns the plugin off |
| `Rumble` | 1 | Controller rumble from the game's force feedback |
| `RumbleStrength` | 100 | Percent, 0–200 |
| `StickDeadzone` | 15 | Percent (the game itself asks for 25) |
| `TriggerThreshold` | 30 | How far a trigger is pulled to count as pressed, percent |
| `InvertAimY` | 0 | Invert vertical aim (on foot and driving only) |
| `SwapSticks` | 0 | Move with the right stick, aim with the left (on foot and driving only) |
| `InputInBackground` | 0 | Keep reading the controller when the game is not the active window |
| `ButtonPrompts` | 1 | 0 keyboard keys, 1 controller buttons while a controller is connected, 2 always |
| `ButtonNames` | auto | `auto` (PlayStation names for PlayStation pads), `xbox`, `playstation` |
| `LogInput` | 0 | Log every controller event (troubleshooting) |

The `[OnFoot]`, `[Driving]` and `[Menus]` sections assign each action to a button. Names:
`A B X Y` (or `CROSS CIRCLE SQUARE TRIANGLE`), `LB RB LT RT` (or `L1 R1 L2 R2`), `LS RS`
(or `L3 R3`), `BACK START GUIDE` (or `CREATE OPTIONS PS`), `DPAD_UP DPAD_DOWN DPAD_LEFT DPAD_RIGHT`,
`TOUCHPAD`, `MISC`, `PADDLE1`–`PADDLE4`, empty for none. Sticks and d-pad always do what the game
expects.

## Troubleshooting

Open `scripts\TCNYCSDL3Pad.log`:

- No log at all: the ASI loader is not loading plugins.
- `SDL3.dll NOT found`: put the 32-bit `SDL3.dll` next to `tcnyc.exe`.
- `Controller connected: ...` lists what SDL3 found; `Controls set: on foot (17 actions ...)` means
  the game accepted the virtual controller.
- `Button prompts: NOT installed`: different `tcnyc.exe` build; controls still work.

## Building

Visual Studio 2022 or newer with the x86 C compiler. Run `build.bat`; the output is
`build\TCNYCSDL3Pad.asi`. No SDL headers or libraries are needed: the few SDL3 functions used are
declared in `sdl3_min.h` and loaded from `SDL3.dll` at runtime.

`test\build_test.bat` builds `test\harness.exe`, which loads the plugin and replays the game's
DirectInput calls using the action tables read from your `tcnyc.exe` (path set at the top of
`harness.c`). It prints which device each action lands on and any controller events.

## How it works

The game uses DirectInput 8 **action mapping** (the DirectX SDK "multiplayer input device manager").
Its actions are numbered after the original Xbox pad buttons, the same numbers in every mode
(0x24 A, 0x26 B, 0x25 X, 0x23 Y, 0x1F/0x20 Black/White, 0x1E Back, 0x1D Start, 0x03/0x18 stick
clicks, 0x21/0x22 triggers, 0x07 d-pad, axes 1/2 left stick, 4/5 right stick).

The plugin hooks `DirectInput8Create`, patches four slots of DirectInput's own function table in
place, hides real controllers from the game and offers a virtual `IDirectInputDevice8A` whose
`BuildActionMap` places each action on an SDL3 input. A worker thread owns SDL3 and publishes
controller snapshots; the device reports only changes, so the game's 10-events-per-frame read never
loses data. A second instance of the same device takes the game's force-feedback path
(`GUID_ConstantForce`, direction = left/right motor) and turns it into `SDL_RumbleGamepad`.

Prompts: text tokens such as `$INPUT_ATTACK$` are resolved by the game to a console button; the
PC-only function at `0x63ED90` then turned that into a keyboard key name. The plugin replaces that
function (after checking its bytes) with one that returns the bound controller button's name.

## Credits

- [SDL](https://www.libsdl.org/) (zlib licence).
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG.

## Licence

MIT, see [LICENSE](LICENSE).
