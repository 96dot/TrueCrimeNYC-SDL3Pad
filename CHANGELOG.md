# Changelog

All notable changes to TCNYCSDL3Pad. Dates are YYYY-MM-DD.

## [0.5.0] - 2026-10-06

### Added
- **Loading screen text at a readable size** (`LoadingScreenText`, on by default). The loading screen
  placed "HINT:", the hint and "LOADING.." at fractions of the screen but drew the letters at the
  font's 640x480 size, so at 3840x2160 they were 4.5 times too small. The game's text function can
  already draw through a scaling matrix kept in the font object, and the game uses it elsewhere; the
  plugin switches it on for those three calls only, at screen height / 480, and scales the two gaps the
  screen measures in unscaled pixels (under the line, and the hint's wrap width) to match. The font is
  put back after each call. Found by pattern like the other patches; at 480 lines or fewer nothing
  changes.

### Tested, and not
- Tested:
  - in game at 3840x2160: both loading screens (start-up and loading a save) show the text at the new
    size, confirmed by eye on a DualSense setup; the log shows all three calls drawn at 4.50x;
  - all automated checks pass, including the new patterns.
- Not tested: other resolutions (1080p would be 2.25x, 1440p 3x), ultrawide, and other builds of the game.

## [0.4.1] - 2026-10-06

Fixes from a full review of the plugin: automated checks, three independent read-only reviews
(security, logic, readability), and every finding checked against the code.

### Fixed
- **Answering "No" on the quit screen with Circle/B no longer also presses B in the menu behind it.**
  The game stops reading input while the prompt is open, then saw that press as new when it started
  again. The plugin now treats input as fresh after the prompt closes, so held buttons are not
  reported as new presses.
- **Reopening the quit screen quickly can no longer confirm "Yes" by itself.** Which presses count
  used to depend on a 300 ms gap; it now depends on the game not reading input, which only happens
  while the prompt is open.
- **Quick re-presses are no longer lost.** If a button was still down at one input read but had been
  released and pressed again before the next, the game saw nothing. It now gets the release, then
  the press. The game reads the controller only every third input call, so this mattered when mashing.
- **Triggers no longer flicker** when resting near `TriggerThreshold`: a pulled trigger now lets go
  5% below it.
- **Quit screen buttons work with `ButtonPrompts=0`.** They used to be installed only when the prompt
  patch was. Only the picture follows `ButtonPrompts` now.
- **The quit screen picture is written safely.** It is written to a temporary file and swapped in, so a
  half-written picture is never used. It is size-checked before use, and rebuilt when the plugin is
  newer than it. Generation from two threads at once is serialised.
- **Text rewriting is more careful with the game's memory.** It checks that the memory is still mapped
  and writable before touching it, and catches any fault. If the read is slow it keeps looking for up
  to 10 seconds, and it logs when a string is no longer in memory. It reuses its slots instead of
  filling up.
- **Rumble moves straight to the next controller** after the one in use is unplugged.
- **SDL3 failures are safe.** If SDL3.dll is missing a function, the plugin now never calls into it.
  The text fix keeps working without SDL3.
- **Building twice before applying a control set** can no longer hand the keyboard's borrowed actions
  (Stealth and others) back to the wrong owner. The hand-back is now bounds-checked.
- **The virtual controller accepts only the structure sizes DirectInput defines** for capabilities,
  object info, device state, properties and effect info.
- **Hardening:**
  - the prompt hook's trampoline is made read-only after it is written;
  - the input-diagnostics device table is locked;
  - the watchdog only pauses threads of the game's own process;
  - `LogInput` stops after 20,000 lines;
  - the exe path is checked before SDL3.dll is loaded.

### Changed
- `tools/` and `.checks.json`: project checks run by `check-project.mjs`. They cover:
  - a build with no warnings;
  - the byte patterns, checked against `tcnyc.exe` the way the plugin uses them (on the original build,
    also the addresses found);
  - an offline replay of the game's DirectInput calls;
  - every setting's default kept in step across code, ini and README.
- The release zip puts the read-me, changelog, licences and write-up in `scripts\TCNYCSDL3Pad\` instead
  of the game folder, so removing that folder removes them too.
- `test/build_test.bat` takes SDL3.dll from the game folder.
- Readability: named constants for source kinds and control sets, compile-time checks on the button
  tables, and duplicate declarations removed.

### Review findings checked and not acted on
- *Diagnostics table race* (security): only reachable if two threads registered devices at once. The game
  does it from one thread, so it was not reachable. Locked anyway, since it costs nothing.
- *Caller-given structure sizes* (security): only the game calls the virtual controller, and it passes
  the standard sizes. Not reachable from outside the game. Tightened anyway.
- *The text buffer may be freed* (logic): checked in a run. The buffer stays in memory, and the log
  records it if it ever goes.
- *Some stick reads in the game use a smaller dead zone than 32/127* (logic): true for a few (at most 7 of 61)
  read sites. With `CancelGameDeadzone=1` those respond slightly earlier. Left as is and documented.

### Second review (same version, before release)
A second round of three independent reviews on the result. Fixed:
- **A trigger could stay pulled for good** with `TriggerThreshold` set to 5 or less: the release point
  (threshold minus 5%) fell to zero or below. The release point is now never below half the threshold.
  Default of 30 was not affected.
- **A fault while scanning the game's text could have frozen the game.** The scan ran under a lock, and a
  fault inside it would have skipped the unlock. The fault handling now sits inside the scan, and the
  lock is released on every path. Never seen; found by reading.
- Quit pictures are no longer generated when they cannot be shown (`ButtonPrompts=0`), nor on the
  input thread, nor when the quit buttons could not be installed.
- A peek read (`DIGDD_PEEK`) no longer counts as the read that clears the quit-screen state.
- The current-control-set address is only published once the code around it has been checked, so the
  diagnostics cannot read a wrong address on another build.
- The text scanner no longer burns a slot on a rejected match, and stops re-scanning a buffer once the
  strings are found.
- `SetProperty` checks the structure size like `GetProperty`.
- The log no longer contains full paths (which can include the Windows user name), and text taken from
  the game's files is logged without control characters, so a modded file cannot forge log lines.
- Small leaks closed: SDL3.dll is unloaded when it is the wrong version, thread handles are closed, the
  prompt trampoline is freed if the patch fails, and the plugin stops cleanly on an over-long path.
- Readability: one count for the control sets, named constants for 0x80, the axis range and the
  DirectInput ten-thousandths, the quit-picture geometry explained, the button-name tables documented
  and counted at compile time, duplicate declaration and a dead flag removed.
- Checks: exact keyboard and mouse counts in the harness check; the 9 bytes the prompt patch verifies
  are read from the C source; a new check that the button names and default layout agree between code,
  ini and README; `ButtonNames` and the Nexus changelog's version are kept in step; a command that
  cannot run without the game is reported as skipped, not failed; `tools/package.py` builds the release
  zip with the documentation in `scripts\TCNYCSDL3Pad\` in the repository's layout, so the README's
  links keep working.

Checked and not acted on:
- *The text table handle is tracked by value and never cleared on close*: a reused handle value could
  make the plugin scan an unrelated read buffer. The scan only ever writes inside that buffer and only
  if it holds exactly the two strings, so the worst case is a wrong string in a buffer that contained
  the string anyway. Left as is.
- *"Press ENTER" is only rewritten when the button's name fits*: Cross, A, B, X, Y and the shoulder
  names do; Square, Circle and Triangle do not, so with those on Select the text stays "Press ENTER".
  Inherent to rewriting in place; documented in the README.
- *Two copies of the plugin under different names* would fight over the log and the prompt patch. User
  error; the second copy logs that its patch was not installed.
- *The checker runs the project's own commands*: by design, like `npm run`; stated in its header.

### Tested, and not
- Tested:
  - all automated checks pass, including the new ones;
  - each check was broken on purpose once and went red;
  - the plugin loads in the game, installs every feature by pattern, and finds the text table.
- Not tested in game yet: the quit-screen fixes and the re-press and trigger changes. They need a
  controller in hand.

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
  game's lettering), and hands it to the game's picture loader (the call at `0x648D0D` to `0x62B4D0`). No game artwork is shipped
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
  pattern must match exactly once (the quit prompt's key check, which the game has twice, may match
  one to four times if every copy uses the same flag), or that feature stays off and the log says so. On the original
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
