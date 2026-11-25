# ETS2 Trailer Control

[🇧🇷 Português](README.md) · **🇺🇸 English**

A plugin for **Euro Truck Simulator 2** plus a window (**TrailerControl.exe**) to couple and
uncouple **every joint** of your combination: truck, trailers and dollies — B-double, road
train, triple, HCT, from the base game and from mods.

| | |
|---|---|
| **Version** | 0.2.0 (experimental) |
| **Game** | ETS2 **1.62.x** — dolly/B-double uncoupling mapped for **1.62.3.3** |
| **OS** | Windows 10/11 64-bit |
| **Mode** | single-player and **Convoy**; in **TruckersMP** internal uncoupling turns itself off |

## Download and install

1. Download **`TrailerControl-...-plugins.zip`** from the
   [Releases](https://github.com/khenrique99/ETS2---Trailer-Control/releases) page2. Extract it **into the game's `plugins` folder**:
   `...\steamapps\common\Euro Truck Simulator 2\bin\win_x64\plugins\`
   (create the `plugins` folder if it does not exist). You should end up with
   `trailer_control.dll`, `trailer_control.ini` and `TrailerControl.exe` there.
3. Start the game and accept the **"SDK features"** warning.
4. **Options → Controls**: on the **Trailer Control** device, bind
   **"Attach / Detach trailer"** (needed to drop the whole set from the truck).
5. In game, press **Ctrl + -**: the window opens.

> Save your game before using it. Dolly/B-double uncoupling is still being tested.

## How to use

- **Ctrl + -** (the `-` key on the main keyboard or the numpad) opens the window over the game.
- Click **Desengatar** (uncouple) / **Engatar** (couple) on the joint you want. The window hides
  and the game comes back; if the game paused while the window was in front, the command runs as
  soon as you return.
- **Esc** or the same shortcut takes you back to the game.
- **Configurações** (settings, bottom of the window): pick the shortcut key — click
  **Alterar…** (change), press the key combination and **Salvar** (save). It applies immediately.
- **Diagnóstico** (diagnostics): writes what the plugin read from memory to `game.log.txt`
  (read-only) — useful when reporting problems.
- **Som da seta** (turn-signal sound, strip above the footer): turns the external turn-signal
  alarm on/off — a beep with each flash of the light while a trailer is coupled. The bar sets the
  **volume from 0 to 100** (click or drag; plays a preview beep). The sound plays through
  Windows: it does not follow the ETS2 volume and is not 3D.
- The plugin watches `trailer_control.ini`: any change (from the window or by hand) applies within
  1 s, no game restart.
- The truck must be stopped (3 km/h limit). To couple an internal joint, the hitches must be
  within 1.5 m of each other.

Other options (max speed, internal uncoupling on/off, distance, words that identify mod dollies)
live in `trailer_control.ini`, with comments explaining each one.

> The window text is in Brazilian Portuguese for now.

## What works

| Feature | Status |
|---|---|
| Detect truck + up to 10 trailers (base game and mods) | ✅ |
| Classify trailer / dolly (configurable keywords for mods) | ✅ |
| Identify the combination: single, B-double, road train, triple, HCT | ✅ |
| Window with the combination drawing and one button per joint | ✅ tested without the game |
| Uncouple/couple **truck ↔ set** | ✅ native game control (Input SDK) |
| Uncouple/couple **internal joints** (dolly, B-double, road train) | 🧪 **1.62.3.3** only, not yet tested in game |
| Real state of internal joints (read from memory) | 🧪 same |
| Shortcut chosen in the window | ✅ tested without the game |
| Internal uncoupling in **Convoy** (`permitir_convoy` in the `.ini`) | 🧪 allowed, tested without the game |
| Turn internal uncoupling off in **TruckersMP** | ✅ tested without the game |
| Turn-signal alarm sound (on/off and volume) | 🧪 logic tested without the game, not yet heard in game |

### How internal uncoupling works

The official SCS SDK can only "press" the coupling control the game already has, and that control
only acts between the truck and the whole set. For internal joints the plugin calls the game's own
routines, found by reverse engineering `eurotrucks2.exe` 1.62.3.3 ([docs/re/](docs/re/README.md),
in Portuguese):

- **Uncouple joint k**: trailer k's "disconnect" routine (removes the physics joint to the trailer
  in front). The game keeps owning the object — nothing is freed.
- **Couple joint k**: the same routine the game uses when it builds the combination, only when
  the hitches are close.

Safety: it only turns on if the exe version is exactly a mapped one, the first bytes of each
function match and every trailer's vtable points to the expected functions; every read and call is
exception-guarded; it turns off in TruckersMP. On any failure that part switches off and the rest
keeps working. After a game update (1.62.4, 1.63…) internal uncoupling stays unavailable until
the table for the new version is added.

## For developers

### Build

Requirements: Visual Studio 2022 or Build Tools (C++ desktop); CMake comes with it.

1. Download the **SCS SDK** from <https://modding.scssoft.com/wiki/Documentation/Engine/SDK/Telemetry>
   and extract it to `sdk/` (`sdk/include/scssdk_telemetry.h` must exist). The SDK is not versioned.
2. Build, test and package:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
```

Output in `dist\`: only the zip for the `plugins` folder (what goes into the release). With
`-TestPackage` it also builds `build\...-teste.zip` (plugin + fake game). Without the SDK, CMake
builds only the window.

### Tests (no game needed)

- `tc_tests` — JSON, command protocol, `.ini` editing, key names, window model.
- `plugin_selftest` — the **fake game** (`tests/fake_game.cpp`) loads the real DLL through the SDK,
  simulates a road train and sends commands like the window does, checking `status.json`.

In-game test plan: [docs/TESTING-IN-GAME.md](docs/TESTING-IN-GAME.md).

### Plugin ↔ window communication

Files in `Documents\ETS2TrailerControl\`: `status.json` (plugin → window, heartbeat every 1 s)
and `command.txt` (window → plugin). Protocol in [src/shared/ipc.h](src/shared/ipc.h).

## License

[MIT](LICENSE) © 2026 Kelvin Henrique Ferreira.

## Disclaimer

Fan project, not affiliated with SCS Software. Euro Truck Simulator 2 is a trademark of SCS
Software. Use at your own risk; internal uncoupling changes the game's behaviour at runtime.
