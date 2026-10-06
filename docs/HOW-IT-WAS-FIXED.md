# How True Crime: New York City (PC) was fixed

This document records what was wrong with the PC version (Aspyr/Luxoflux, 2006), how each problem
was tracked down, and what the fix does. Everything was worked out by reading `tcnyc.exe`, watching
the game through our own plugins' logs, and testing in game on a DualSense controller.

All addresses refer to `tcnyc.exe`, 20,135,936 bytes, MD5 `b7eee2f3f4c2014d235acf238716b495`.
The executable is protected with SafeDisc: much of the code is normal, but some functions live in an
extra `.rld` section where calls are rewritten as `push <return>; jmp <target>` with junk bytes in
between, and some call targets are only known at runtime.

Contents:
1. [Controller support](#1-controller-support)
2. [Button prompts](#2-button-prompts)
3. [The game's hidden stick dead zone](#3-the-games-hidden-stick-dead-zone)
4. [Frame rate and stutter](#4-frame-rate-and-stutter)
5. [Picture quality](#5-picture-quality)
6. [Working alongside the new widescreen fix](#6-working-alongside-the-new-widescreen-fix)
7. [Still open: dark blobs on walls](#7-still-open-dark-blobs-on-walls)
8. [Things that were tried and did not help](#8-things-that-were-tried-and-did-not-help)
9. [Address reference](#9-address-reference)

---

## 1. Controller support

### What was wrong

PCGamingWiki: "XInput controllers mapping is all over the place and can't be changed."

The game never reads a controller directly. It uses **DirectInput 8 action mapping**, built on the
DirectX SDK's "multiplayer input device manager" sample (code around `0x645000`). It gives DirectInput
a list of actions with abstract *semantics* and lets DirectInput decide which physical control
does what:

| Semantic (value) | Meaning |
|---|---|
| `DIAXIS_ANY_X_1` (`FF00C201`), `DIAXIS_ANY_Y_1` (`FF014201`) | any X / Y axis (left stick) |
| `DIAXIS_ANY_U_1` (`FF02C201`), `DIAXIS_ANY_V_1` (`FF034201`) | "U" / "V" axes (meant as the right stick) |
| `DIAXIS_ANY_Z_1` (`FF01C201`) | Z axis (Target / Fire on one axis) |
| `DIBUTTON_ANY(0..11)` (`FF004400`+n) | "button number n" |
| `DIPOV_ANY_1` (`FF004601`) | any hat switch (d-pad) |
| `0x28xxxxxx` | the "browser" genre, used for menus |

There are three action tables, one per control set: on foot (`0x75D668`, genre `0x0A000000`),
driving (`0x75DFC8`, genre `0x02000000`) and menus (`0x75CFD8`, genre `0x28000000`).

The readme says a pad "must be able to mimic an Xbox controller and must have 12 programmable
buttons". The button order is the original Xbox pad: A, B, X, Y, Black, White, Back, Start, LS, RS,
LT, RT. On a modern pad, DirectInput's guesses go wrong:

- the right stick lands on the wrong axes, so aiming is broken;
- both triggers share one Z axis. The game reads positive as Target Lock and negative as Fire, so the
  two cannot be held together;
- buttons 10 and 11 (Target Lock, Fire) do not exist on an XInput pad at all.

The key discovery: the action numbers (`uAppData`) **are the Xbox buttons**, and they are the same
in every control set. In the menu table the keyboard keys are even labelled "A", "B", "X", "Y",
"left trigger", "right trigger" against those numbers. `0x63EAF0` converts the game's internal
button bitmask into these numbers.

| Action | Xbox button | Action | Xbox button |
|---|---|---|---|
| `0x24` | A | `0x1D` | Start |
| `0x26` | B | `0x1E` | Back |
| `0x25` | X | `0x03` | Left stick click |
| `0x23` | Y | `0x18` | Right stick click |
| `0x1F` | Black (LB) | `0x21` | Left trigger |
| `0x20` | White (RB) | `0x22` | Right trigger |
| `0x07` | d-pad (hat) | `0x19`-`0x1C` | d-pad up / down / left / right as buttons |
| `0x01`/`0x02` | left stick Y / X | `0x04`/`0x05` | right stick Y / X |

### How the game reads input (`0x63DE70`)

- Each call reads **one** device, in rotation (index `0x84A940`), with `Poll`, then `Acquire` on
  failure, then `GetDeviceData` for **at most 10 events**.
- A button counts as pressed only when the event value is exactly `0x80`.
- The hat value is divided by 4500 into eight directions, and those direction bits are **ORed in
  without being cleared**, so the game must see "centred" between two directions.
- Axis range is -127..127 (set in the action format at `0x63DC90`), with a 16-event buffer.
- Events are dispatched through a jump table at `0x63E7C8` into bitfields at `0x84A904`/`0x84A905`
  that mirror the Xbox pad.

### The fix

`TCNYCSDL3Pad.asi` hooks `DINPUT8.dll!DirectInput8Create` in the exe's import table, then patches
four slots of DirectInput's own `IDirectInput8A` function table *in place* (swapping the whole table
is rejected by DirectInput). It acts only on objects the game created:

- **`EnumDevicesBySemantics` / `EnumDevices`:** real controllers are hidden from the game and one
  virtual controller is offered instead. Keyboard and mouse pass through untouched.
- **The virtual controller** is a complete `IDirectInputDevice8A`. Its `BuildActionMap` places every
  action on the matching SDL3 gamepad input, per control set, from the ini. It also borrows
  keyboard-only actions the player puts on a button (Stealth, Endo, ...) and hands those table
  entries back to the keyboard right after `SetActionMap`.
- **`GetDeviceData`** reports only controls whose value changed since the game last read them.
  Nothing queues up, so the 10-events-per-frame limit can never drop a button release. Taps
  shorter than a frame are kept with a press counter, and "centred" is inserted between hat directions.
- **SDL3** runs on its own thread (hot-plug, the most recently pressed controller wins) and is loaded
  from `SDL3.dll` at runtime. No SDL headers or libraries are needed to build (`sdl3_min.h`).

### Rumble

The game opens a second device for force feedback (`EnumDevices(GAMECTRL, FORCEFEEDBACK)`,
`0x63DD70`) and drives one `GUID_ConstantForce` effect (`0x63E890`). The two values in
`rglDirection` are the left and right motor strengths. The virtual controller reports two
force-feedback axes and implements `IDirectInputEffect`, and its `SetParameters` / `Start` / `Stop` become
`SDL_RumbleGamepad`.

### Verification

`test/harness.c` loads the plugin the way the ASI loader does and replays the game's DirectInput
calls with the real action tables read from `tcnyc.exe`. It checks which device each action lands on
(keyboard 34 / mouse 8 / controller 17 actions on foot) and that the keyboard keeps its bindings.

---

## 2. Button prompts

Text in `Data\Lang\*\LangTable.dat` uses tokens such as `$INPUT_ATTACK$`, `$INPUT_LSTICK$` and
`$DPAD_LEFT$`. A resolver in the SafeDisc section (`0x1473990`) turns the token into a number, and the
console logic turns that into a console pad button. Only the last step, which is PC-only, turns the
button into the name of the **default keyboard key** (`0x63ED90`, `const char *(int buttonMask)`,
reached through a jump stub at `0x4A0090`).

The plugin replaces that function after checking its first 9 bytes. Its version turns the button
into an action (`0x63EAF0`), looks up the controller button that action is on in the current
control set (`0x75CCC0`), and returns its name: Xbox names, or PlayStation names when a PlayStation
pad is in use. Without a controller the original keyboard names are kept. Because of this, prompts
follow the player's remapping.

---

## 3. The game's hidden stick dead zone

The game's axis reader `0x40C900(axis, deadzone)` takes the byte the input code produced, discards
anything at or below `deadzone`, and rescales the rest. At least 54 of its 61 call sites pass `0x20`
(32 of 127, about 25% per axis). It also asks DirectInput for a 25% dead zone (`DIPROP_DEADZONE`,
callback `0x63DC20`). The plugin ignores that request and applies its own round dead zone
(`StickDeadzone`, default 15%).

With both active, about the first third of stick travel did nothing. `CancelGameDeadzone=1` sends
`32 + value * 95` instead of `value * 127`. The game's rescale then returns exactly the plugin's
value, so only `StickDeadzone` is felt.

---

## 4. Frame rate and stutter

### The original timing

- A timer thread (`0x647D80`) adds 1 to a tick counter (`0x84A054`) whenever `timeGetTime` has moved
  on by 17 ms or more, about 59 ticks a second.
- The frame wait (`0x647E70`) waits for 2 ticks per frame, and the main loop (`0x49BB5x`) runs up to 2
  simulation steps per frame (clamp at 4). The result is the 30 fps cap. Its coarse, sleep-based
  timing also produces uneven frames.

### ThirteenAG's widescreen fix (2026-05-30)

ThirteenAG's [widescreen fix](https://github.com/ThirteenAG/WidescreenFixesPack) adds a proper 60 fps
mode:
- it replaces the timer thread with a precise one;
- it removes the frame wait;
- it makes the main loop run one step per frame (`0x49BB8B`);
- it scales the step size and paces frames with its own limiter before Present.

It also runs the game through **dxwrapper** (Direct3D 8 to 9) for its post-processing effects. That
lifted the cap, but driving was still choppy.

### Measuring it

A diagnostic plugin (`gfxdiag/`) timed every Present. It recorded frame times, time spent *inside*
Present, time spent creating textures, and the main thread's CPU use; a separate logger recorded
GPU utilisation. While driving through the city, with the game on dxwrapper and Windows' Direct3D 9:

| | On foot | Driving |
|---|---|---|
| Frame rate | 55-60 fps | 37-48 fps |
| Waiting in Present per frame | 8-10 ms | ~12 ms |
| Game thread busy | ~35% | ~35% |
| GPU 3D engine busy | low | ~23% |

Neither the CPU work nor the GPU was the limit: the game finished its own work in about half a frame
and then waited in Present while the GPU sat mostly idle. Stack samples taken during waits pointed
into AMD's Direct3D 9 driver (`AMDXN32.DLL`).

### The fix: DXVK

[DXVK](https://github.com/doitsujin/dxvk) runs Direct3D 9 on Vulkan. With its `d3d9.dll` next to
`tcnyc.exe`, dxwrapper uses it automatically. Measured the same way:

| | dxwrapper on Windows' D3D9 | dxwrapper on DXVK |
|---|---|---|
| Driving frame rate (settled) | 37-55 fps | **56-60 fps** |
| Game thread busy | 35-40% | 22-28% |
| Present wait | the frame arrives late | the frame is ready early and waits for the next refresh |

The first minutes in new areas show some 1-2.5 second freezes while pipelines compile for the first
time. The AMD driver caches them, so later sessions should freeze less. This was not yet measured
when this was written.

With DXVK presenting in step with the display (vsync), the widescreen fix's own limiter is not
needed (`FrameLimitType = -1`). This relies on a **60 Hz display**. The game runs one simulation step
per frame, so on a faster display it would run faster than intended. There, keep the fix's limiter
(`FrameLimitType = 1`) or cap DXVK with `d3d9.maxFrameRate = 60` in `dxvk.conf` (not tested).

---

## 5. Picture quality

The widescreen fix's post-processing defaults made the image hazy:
- **Bloom** at a fixed 2x intensity;
- **DistantBlur**, a depth-of-field blur on everything past a set distance;
- **ConsoleGamma**, which brightens mid-tones like an Xbox 360 on a TV.

The settings used here: `Bloom = 0`, `DistantBlur = 0`, `ConsoleGamma = 1`, `AntiAliasing = 1` (SMAA),
`HighResolutionShadows = 1`.

---

## 6. Working alongside the new widescreen fix

After updating to the 2026-05-30 widescreen fix, input misbehaved:

- With `RawInputMouse = 1` the fix's raw mouse code drove the camera directly whenever the game was not
  paused, loading or in a cutscene. This included the main menu: the mouse turned the camera
  behind the menu and the cursor disappeared. **Recommendation: `RawInputMouse = 0`.**
- In one session no input worked at all on the main menu. That session started its input system
  twice, but it could not be reproduced afterwards. A later "no input" report turned out to be the
  quit screen, which only accepts the Y and N keys. `DiagInput=1` was added to catch it if it happens
  again.

What was ruled out: neither plugin is loaded twice; none of the fix's 82 byte patterns overlap the
bytes this plugin patches; the bundled XP-era `dimap.dll` maps keyboard and mouse exactly like
Windows 11's built-in mapper.

---

## 7. Still open: dark blobs on walls

Dark, soft blobs appear on walls, for example behind the punching bag in the police academy. They move
with the camera and the player. They are **projected shadows** reaching surfaces far behind the
object casting them, not missing textures:
- every texture the game creates succeeds, in ordinary formats (DXT1, DXT5, A8R8G8B8);
- the blobs were already there with the old fix on plain Direct3D 8;
- turning `HighResolutionShadows` off changes nothing.

Fixing this needs the code that decides which surfaces receive a shadow. It has not been found yet.

---

## 8. Things that were tried and did not help

- **Recycling render targets.** The game creates and destroys several render targets every frame
  (shadow and effect buffers) through `0x63FD60` / `0x63FDA0`: about 250 a second at the menu. A
  pool that kept and reused them was built, but measured worse frame pacing. It is left off
  (`gfxdiag`, `RenderTargetPool=0`). The measurement was later found to be unreliable (see below),
  so this is not conclusive.
- **Forcing vsync in dxwrapper** with the fix's limiter off: measured worse, with the same caveat.
- **The fix's other limiter mode** (`FrameLimitType = 2`): no clear difference.
- **A caution on measuring:** game sessions started by a script while another window was open on the
  same display gave unreliable numbers. The same settings measured 60 fps in one run and 30 in the
  next. All conclusions above come from the player's own sessions.

---

## 9. Address reference

| Address | What |
|---|---|
| `0x694C40` / IAT `0x6CC02C` | `DirectInput8Create` thunk / import |
| `0x63DD70` | rumble device set-up (enumerates force-feedback controllers) |
| `0x63E890` | rumble update (constant force, direction = left/right motor) |
| `0x63DC90` | builds the action format for a control set (range -127..127, buffer 16) |
| `0x63DE70` | per-call input read (one device in rotation, 10 events max) |
| `0x63E7C8` | action dispatch jump table |
| `0x84A904` / `0x84A905` | the game's merged pad-style button bits |
| `0x63EAF0` | console button mask to action number |
| `0x63ED90` | button mask to key name (replaced by the plugin) |
| `0x75CCC0` | current control set (0 on foot, 1 driving, 2 menus) |
| `0x63F5E0` | input system set-up (called from start-up `0x49BED8`, setup dialog `0x651C29`, reset `0x6520F7`) |
| `0x645C80` / `0x645B30` | control manager creation / device enumeration and action maps |
| `0x40C900` | stick axis reader with dead zone (usually `0x20`) |
| `0x647D80` | original 17 ms timer thread (tick counter `0x84A054`) |
| `0x647E70` | original frame wait |
| `0x63FA70`, `0x63FD60`, `0x63FDA0` | texture object create / init / recreate |
| `0x1473990` | token name to number (SafeDisc `.rld` section) |
