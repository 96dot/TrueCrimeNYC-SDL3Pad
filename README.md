# TCNYCSDL3Pad

Proper controller support for **True Crime: New York City** (PC, 2006), through SDL3, plus a tested
recommended setup for 60 fps and smooth driving.

- Correct controls on modern controllers (Xbox, PlayStation, Switch and most others): right-stick
  aiming, Target Lock and Fire on the triggers (and usable together), d-pad weapon selection.
- Rumble.
- On-screen hints show controller buttons ("Press Cross to Save Game") and follow your remapping,
  including the few hints the game wrote out as keyboard keys ("Press ENTER"; that one is rewritten in
  memory but has not been seen in game yet).
- The quit screen ("Are you sure you want to quit? Y/N") takes A/Cross and B/Circle, and shows them.
- Every action remappable per mode, including actions the PC version only had on the keyboard.
- The game's own extra stick dead zone removed, so small stick movements register.
- Hot-plug, no stuck or lost buttons.

See [CHANGELOG.md](CHANGELOG.md) for every version, and
[docs/HOW-IT-WAS-FIXED.md](docs/HOW-IT-WAS-FIXED.md) for the full story of what was wrong and how
each part was fixed.

## Requirements

- True Crime: New York City, PC. Developed and tested on `tcnyc.exe` 20,135,936 bytes,
  MD5 `b7eee2f3f4c2014d235acf238716b495` (SafeDisc retail, April 2006). Nothing is tied to that exact
  file. Controller input goes through DirectInput itself, and the prompt and quit-screen patches find
  their code by byte pattern, so other builds with the same code should work. Any feature whose
  code is not found stays off and the log says so. Only this build has been tested.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) as `dinput8.dll`. It is
  included with ThirteenAG's widescreen fix (below).
- `SDL3.dll`, **32-bit (x86)**, version 3.2 or newer. It is included in the release zip, or available
  from [SDL releases](https://github.com/libsdl-org/SDL/releases) (`SDL3-3.x.x-win32-x86.zip`).

## Install

1. Install the recommended setup below (or at least the Ultimate ASI Loader).
2. Extract the release zip into the game folder (next to `tcnyc.exe`). It adds
   `scripts\TCNYCSDL3Pad.asi`, `scripts\TCNYCSDL3Pad.ini` and `SDL3.dll`, plus this read-me, the
   changelog and the licences in `scripts\TCNYCSDL3Pad\`.
3. Start the game. `scripts\TCNYCSDL3Pad.log` is written on every launch.

Once a controller is in use, the plugin also creates `scripts\TCNYCSDL3Pad\QuitGame_Xbox.pct`
and `QuitGame_PlayStation.pct`: copies of the game's quit-screen picture with "Y/N" replaced by
controller buttons, drawn from your own game files.

To remove: delete those files and the `scripts\TCNYCSDL3Pad` folder.

## Recommended setup

These are third-party projects. Download them from their own pages; they are not included here.

| What | Where | Why |
|---|---|---|
| ThirteenAG's widescreen fix (2026-05-30 or later) | [WidescreenFixesPack release "truecrimenyc"](https://github.com/ThirteenAG/WidescreenFixesPack/releases/tag/truecrimenyc) | 60 fps mode, widescreen HUD/FOV, ASI loader, dxwrapper (Direct3D 8 to 9) |
| DXVK, 32-bit `d3d9.dll` | [DXVK releases](https://github.com/doitsujin/dxvk/releases) (`x32` folder) | Runs Direct3D 9 on Vulkan. Fixed the slowdowns while driving on an AMD RX 9070 XT |

Copy DXVK's 32-bit `d3d9.dll` next to `tcnyc.exe`. dxwrapper picks it up automatically.

Settings used in `scripts\TrueCrimeNewYorkCity.WidescreenFix.ini`:

| Setting | Value | Note |
|---|---|---|
| `Enable60FPS` | 1 | |
| `FrameLimitType` | -1 | Let DXVK and the display's vsync pace the game. **Only on a 60 Hz display.** On a faster display use `1`, or cap DXVK at 60 (see below) |
| `RawInputMouse` | 0.0 | With 1, the mouse turned the camera behind the main menu and the cursor disappeared |
| `AntiAliasing` | 1 | SMAA |
| `ConsoleGamma` | 1 | Xbox 360-style brightness |
| `Bloom` | 0 | Strong and fixed in strength; made the picture hazy |
| `DistantBlur` | 0 | Blurs everything in the distance |
| `HighResolutionShadows` | 1 | |

Measured while driving through the city (RX 9070 XT, 3840x2160, 60 Hz):

| Setup | Driving frame rate |
|---|---|
| Old widescreen fix, Direct3D 8 | 29-30 fps (the game's own cap) |
| New widescreen fix (dxwrapper, Windows' Direct3D 9) | 37-55 fps, choppy |
| New widescreen fix + DXVK | 56-60 fps once warmed up |

With DXVK the first minutes in new areas can freeze for 1-2 seconds while effects compile for the
first time. The graphics driver caches them, so later sessions should freeze less (not measured yet).

On a display faster than 60 Hz, either keep `FrameLimitType = 1`, or put `d3d9.maxFrameRate = 60` in
a `dxvk.conf` next to `tcnyc.exe` (not tested).

## Default controls

The game was designed for the original Xbox controller. Its Black and White buttons are LB and RB here.

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
| `RumbleStrength` | 100 | Percent, 0-200 |
| `StickDeadzone` | 15 | Percent, round, measured on the whole stick |
| `CancelGameDeadzone` | 1 | Cancel the game's own 25%-per-axis dead zone; 0 keeps the original feel |
| `TriggerThreshold` | 30 | How far a trigger is pulled to count as pressed, percent |
| `InvertAimY` | 0 | Invert vertical aim (on foot and driving only) |
| `SwapSticks` | 0 | Move with the right stick, aim with the left (on foot and driving only) |
| `InputInBackground` | 0 | Keep reading the controller when the game is not the active window |
| `ButtonPrompts` | 1 | 0 keyboard keys, 1 controller buttons while a controller is connected, 2 always |
| `ButtonNames` | auto | `auto` (PlayStation names for PlayStation pads), `xbox`, `playstation` |
| `QuitScreenButtons` | 1 | Quit screen takes A/Cross = Yes, B/Circle = No and shows those buttons |
| `LogInput` | 0 | Log every controller event (troubleshooting) |
| `DiagInput` | 0 | Log input diagnostics: device reads, the game's button state, a watchdog if the game stops reading input (troubleshooting) |

The `[OnFoot]`, `[Driving]` and `[Menus]` sections assign each action to a button. Names:
`A B X Y` (or `CROSS CIRCLE SQUARE TRIANGLE`), `LB RB LT RT` (or `L1 R1 L2 R2`), `LS RS`
(or `L3 R3`), `BACK START GUIDE` (or `CREATE OPTIONS PS`), `DPAD_UP DPAD_DOWN DPAD_LEFT DPAD_RIGHT`,
`TOUCHPAD`, `MISC`, `PADDLE1`-`PADDLE4`, empty for none. The sticks and d-pad always do what the game
expects. Also accepted: `VIEW` and `SELECT` (Back), `SHARE`, `MENU` (Start), `HOME` (Guide), and `MIC`, `MUTE` and
`CAPTURE` (Misc).

## Known issues

- **Dark blobs on some walls.** These are shadows projected too far onto surfaces behind the object
  casting them. The game does this by itself (also with the old fix and on plain Direct3D 8). Not fixed.
- **Shader warm-up freezes with DXVK** in the first minutes, as described above.

## Troubleshooting

Open `scripts\TCNYCSDL3Pad.log`:

- No log at all: the ASI loader is not loading plugins.
- `SDL3.dll NOT found`: put the 32-bit `SDL3.dll` next to `tcnyc.exe`.
- `Controller connected: ...` lists what SDL3 found. `Controls set: on foot (17 actions ...)` means
  the game accepted the virtual controller.
- The second line names your exe build. `Button prompts: NOT installed` or `Quit screen buttons: NOT
  installed` means that part of the game's code was not found in your build. Controls still work.
- Input stops responding: set `DiagInput=1`, reproduce, quit after about 5 seconds, and read the log.

## Checking

`node tools/check-project.mjs` runs the project checks listed in `.checks.json`: build with no warnings,
the byte patterns against your `tcnyc.exe`, the offline DirectInput replay, and settings kept in step
across code, ini and README. It needs Node 18 or later and Python, and the game one folder up.

## Building

Visual Studio with the x86 C compiler. `build.bat` and `test\build_test.bat` look for Visual Studio 2026
(version 18) Community; edit the `vcvarsall.bat` path in them for another edition. Run `build.bat`; the output is
`build\TCNYCSDL3Pad.asi`. No SDL headers or libraries are needed: the few SDL3 functions used are
declared in `sdl3_min.h` and loaded from `SDL3.dll` at runtime.

- `test\build_test.bat` builds `test\harness.exe`. It loads the plugin and replays the game's
  DirectInput calls using the action tables read from your `tcnyc.exe` (path at the top of `harness.c`; it
  reads them at fixed offsets of the original build, unlike the plugin).
- `gfxdiag\build.bat` builds `TCNYCGfxDiag.asi`, a diagnostic plugin used during development. It
  logs Direct3D 8 texture formats and failures, render-target creators, frame times, time waiting in
  Present and main-thread CPU use. It is not part of the release; drop it into `scripts\` only when
  investigating.

## How it works (short version)

The game uses DirectInput 8 **action mapping**. Its actions are numbered after the original Xbox
pad buttons, the same numbers in every mode. The plugin hooks `DirectInput8Create`, patches four
slots of DirectInput's own function table in place, hides real controllers from the game and offers
a virtual `IDirectInputDevice8A`. Its `BuildActionMap` places each action on an SDL3 input. The
device reports only changes, so the game's 10-events-per-frame read never loses data. The same
device takes the game's force-feedback effect and turns it into rumble. A PC-only function that
turns console buttons into keyboard key names (`0x63ED90`) is replaced, after checking its bytes, to
give controller button names. The two strings that name keys literally are rewritten in the game's
text table as it is loaded, and the quit screen's Y/N key checks also accept the controller. The full write-up is in [docs/HOW-IT-WAS-FIXED.md](docs/HOW-IT-WAS-FIXED.md).

## Credits and licences

| Component | Used how | Licence |
|---|---|---|
| [SDL 3](https://www.libsdl.org/) | `SDL3.dll` included unmodified in the release zip | zlib, see `licenses/SDL3-LICENSE.txt` |
| [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG | Required, not included | MIT |
| [WidescreenFixesPack](https://github.com/ThirteenAG/WidescreenFixesPack) by ThirteenAG | Recommended, not included | MIT |
| [dxwrapper](https://github.com/elishacloud/dxwrapper) by elishacloud | Part of the widescreen fix, not included | zlib-style, with bundled components under their own licences |
| [DXVK](https://github.com/doitsujin/dxvk) | Recommended, not included | zlib |

## Licence

MIT, see [LICENSE](LICENSE).
