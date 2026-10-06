# Changelog

All notable changes to TCNYCSDL3Pad. Dates are YYYY-MM-DD.

## [0.4.0] - 2026-10-06

### Added
- **Quit screen works with a controller** (`QuitScreenButtons`, on by default). The quit prompt
  stops reading DirectInput and asks Windows directly for the Y and N keys (`GetAsyncKeyState` at
  `0x4A8D57` and `0x4BE3E4`, while flag `0x793359` is set). Those two checks now also answer from the
  controller: A/Cross = Yes, B/Circle = No. Only presses made after the prompt opened count, so the
  press that chose "Quit" cannot confirm it by itself. The keyboard keeps working.
- **Quit screen picture names the buttons.** The prompt is a picture (`Data\Shell\QuitGame.pct`,
  640x448, with "Y/N" painted in). The plugin draws its own copy from the player's game file,
  "A: Yes  B: No" or "Cross: Yes  Circle: No" in Trebuchet MS Bold (the closest Windows font to the
  game's lettering), and hands it to the game's picture loader (`0x648D0D`). No game artwork is shipped
  with the plugin. If the game cannot load the copy it is given the original, because a failed load
  of this picture makes the game quit at once.
- **Hard-coded key names in text.** Only two strings in the game's text table name keys literally:
  - "Press ENTER" now reads "Press Cross" or "Press A" (the menus' Select button). It falls back
    to the original if the button's name does not fit;
  - the PC disk-space message drops the Xbox leftover "or B to free more blocks".
  The table is caught as the game reads it (overlapped `ReadFile`/`ReadFileEx` on `LangTable.dat`)
  and the strings are rewritten in place, and back again when no controller is connected.
  "'Enter' / 'Backspace' / 'Ctrl-Enter'" on the name-typing screen are left alone, since that
  screen reads the keyboard only.

### Changed
- **No longer tied to one exe.** The prompt and quit-screen patches used fixed addresses for one
  build. They now find their code by byte pattern at startup and read the addresses they need from
  the instructions they find (the current control set, the quit flag, the picture loader). Every
  pattern must match exactly once, or that feature stays off and the log says so. On the original
  build every pattern lands on the same address as before. Controller input, rumble, the dead-zone
  fix and the text fix never depended on addresses.
- The log's second line names the exe build. The input diagnostics' game-state readings
  (`DiagInput`) are only taken on the original build.

### Notes
- Quit screen (picture, Cross = Yes, Circle = No) confirmed working in game on a DualSense, on the
  build just before the switch to patterns, which resolve to the same addresses.
- "Press ENTER" is found and rewritten (confirmed in the log) but has not been seen in game yet.
- Text changes only match the English wording (English and UK folders).
- Other builds of the game are untested.

## [0.3.0] - 2026-10-05

### Added
- **`CancelGameDeadzone`** (on by default). The game applies its own dead zone to every stick read:
  its axis reader (`0x40C900`) throws away the first 32 of 127 steps on each axis (about 25%) and
  rescales the rest. At least 54 of its 61 call sites pass that value; a few pass smaller ones. Together with this plugin's own round
  dead zone, roughly the first third of stick travel did nothing. The plugin now sends values that
  start just past the game's threshold, so the game's rescale lands exactly on the plugin's
  `StickDeadzone` and small stick movements register. `CancelGameDeadzone=0` restores the original feel.
- **`DiagInput`** troubleshooting mode (off by default). Every 2 seconds it logs how often the game
  reads the keyboard, mouse and controller, what DirectInput returns and how many events each device
  delivers. It also logs the game's own merged button state, its current control set, the window its
  input is bound to, which code path set DirectInput up, and a watchdog that records where the game's
  main thread is if it stops reading input for more than 1.5 s.
- Log lines carry the process id and module address, so two copies of the game or plugin can be
  told apart.
- `gfxdiag/TCNYCGfxDiag.asi`, a separate diagnostic plugin used to investigate performance (not part
  of the release): Direct3D 8 texture formats and failures, render-target creators, frame times, time
  spent waiting in Present, and main-thread CPU use.
- `docs/HOW-IT-WAS-FIXED.md`: a full write-up of the investigation and every fix.
- `licenses/SDL3-LICENSE.txt`.

### Recommended setup (documented in the README)
Measured on the author's machine (RX 9070 XT, 3840x2160 at 60 Hz) while driving through the city:

| Setup | Driving frame rate |
|---|---|
| Old widescreen fix (Direct3D 8, native 30 fps cap) | 29-30 fps |
| ThirteenAG widescreen fix 2026-05-30 (60 fps mode, dxwrapper to Direct3D 9) | 37-55 fps |
| Same, plus DXVK for Direct3D 9 | 56-60 fps once warmed up |

### Notes
- Works alongside ThirteenAG's widescreen fix (2026-05-30). Its `RawInputMouse` option should be `0`:
  with it on, the mouse turned the camera behind the main menu and the cursor disappeared.
- Confirmed working in game on a DualSense.

## [0.2.0] - 2026-10-05

### Added
- **Controller button prompts.** On-screen hints ("Press ... to Save Game", "Hold ... : Use Stove",
  tutorial and loading-screen tips) now name controller buttons instead of keyboard keys.
  - The game still builds every hint the console way, as a console pad button. The PC port only
    turned that button into the name of the *default* keyboard key at the very last step
    (`0x63ED90`). That step is replaced: button → game action → the controller button the ini puts
    that action on → its name. Hints therefore follow your own remapping, which the original never did.
  - Xbox names (A, B, X, Y, LB, RB, LT, RT, Back, Start, LS, RS) or PlayStation names (Cross, Circle,
    Square, Triangle, L1, R1, L2, R2, Create, Options, L3, R3), picked from the controller in use.
  - D-pad hints (weapon / combat-mode / song selection) show "D-pad Up/Down/Left/Right".
  - Without a controller connected the original keyboard key names are shown.
  - The function's bytes are checked before patching; on any other `tcnyc.exe` the log says
    "Button prompts: NOT installed" and the game is left untouched.
  - The log records each prompt type once (`Prompt: console button 0100 in on foot -> "Cross"`).
- ini settings `ButtonPrompts` (0 = keyboard keys, 1 = controller names while a controller is
  connected, 2 = always) and `ButtonNames` (`auto`, `xbox`, `playstation`).

### Notes
- Confirmed working in game on a DualSense.

## [0.1.0] - 2026-10-05

First release. Confirmed working in game (input and rumble) on a DualSense.

### Why it was needed
True Crime: New York City never reads a controller itself. It gives DirectInput's *action mapper*
a list of requests ("any X axis", "any button 0..11", "any hat switch") laid out for an original
Xbox pad, and lets Windows decide which physical control does what. On modern controllers that
guessing goes wrong ("XInput controllers mapping is all over the place and can't be changed",
PCGamingWiki):
- the right stick's axes come out swapped or missing, so aiming is broken;
- both triggers share one axis, so Target Lock and Fire cannot be held at the same time;
- buttons 10 and 11 (Target Lock and Fire) do not exist on an XInput pad at all;
- noisy analog input fills DirectInput's small event buffer, which can swallow button releases
  (controls "locking up").

### Added
- **Virtual SDL3 controller.** Real controllers are hidden from the game's DirectInput and one
  virtual controller is offered instead. When the game asks it to map its controls, every action is
  placed directly on the matching SDL3 gamepad input. Works with Xbox, PlayStation, Switch and
  most other controllers SDL3 supports.
- **Original Xbox layout by default** (the game's readme asks for a pad that can "mimic an Xbox
  controller" with 12 buttons; the Xbox's Black/White buttons become LB/RB):
  left stick move, right stick aim, LT Target Lock, RT Fire, d-pad weapon/combat mode,
  A/B/X/Y light attack / grapple / heavy attack / jump (accelerate / handbrake / brake / look behind
  in vehicles), LB use, RB reload/block, Back badge/siren, Start pause/map, LS crouch, RS precision aim.
- **Remapping** of every action, per mode (on foot, driving, menus), in `TCNYCSDL3Pad.ini`.
  PlayStation button names are accepted (CROSS, L2, ...).
- **Keyboard-only actions on the controller.** Actions the PC version only offered on the keyboard
  (Stealth, previous/next combat mode, next weapons, Endo, Wheelie, camera modes, songs) can be put
  on any spare button. The keyboard keeps its own binding for them.
- **Rumble.** The game's force-feedback effect (one constant force whose direction carries the
  left and right motor strength) drives the controller's motors through SDL3. `Rumble`,
  `RumbleStrength` settings.
- **No lost or stale input.** The virtual controller reports only what changed since the game last
  read it, so the game's 10-events-per-frame limit can never drop a button release or build a
  backlog. Taps shorter than one frame are still delivered.
- **Hot-plug.** Controllers can be connected or swapped at any time; the controller pressed last is
  the one in use.
- **Foreground-only input** like the original DirectInput device; `InputInBackground` to change it.
- Settings: `StickDeadzone` (radial, default 15% instead of the game's 25%), `TriggerThreshold`,
  `InvertAimY`, `SwapSticks` (both gameplay-only, menus unaffected), `Enabled`, `LogInput`.
- `TCNYCSDL3Pad.log`, rewritten on every launch: settings, layouts, controllers found, each control
  set the game builds, rumble setup.
- `SDL3.dll` is loaded at runtime from the game folder (or next to the plugin). If it is missing the
  log says so and the game runs with keyboard and mouse as before.
- Offline test program (`test/harness.c`) that loads the plugin and replays the game's exact
  DirectInput calls with the action tables read from `tcnyc.exe`.

### Technical
- Hooks only `DINPUT8.dll!DirectInput8Create` in the exe's import table, then patches four slots
  of the real `IDirectInput8A` function table in place (CreateDevice, EnumDevices, GetDeviceStatus,
  EnumDevicesBySemantics) and acts only on DirectInput objects the game created. No game code is
  patched by this version.
- Keyboard and mouse mappings are unaffected (verified: 34 / 8 actions on foot, 32 / 7 driving,
  25 / 7 menus). Keyboard table entries the controller borrows are handed back right after use.
