# Trailer Control 0.2.0 — in-game test guide

[🇧🇷 Português](TESTE-NO-JOGO.md) · **🇺🇸 English**

This version was built and tested **without the game** (with a "fake game" that loads the real
plugin). What still needs confirming is the behaviour inside ETS2 — mainly dolly / B-double
uncoupling, which works on the game's memory. If you can, run the checklist below and report
back.

> **Before you start:** save your game. Internal uncoupling only turns on in ETS2 **1.62.3.3**, in
> single-player or in **Convoy**; on another version or in TruckersMP it turns itself off and only
> truck uncoupling works.

> The window text is in Brazilian Portuguese for now: **Engatar** = couple, **Desengatar** =
> uncouple, **Engatada** = coupled, **Solta** = uncoupled, **Configurações** = settings,
> **Diagnóstico** = diagnostics, **Alterar** = change, **Salvar** = save.

## Files

| File | Purpose |
|---|---|
| `trailer_control.dll` | the plugin |
| `trailer_control.ini` | settings (the shortcut can be changed from the window) |
| `TrailerControl.exe` | the coupling window — the in-game shortcut opens it |

## 1. Install

1. Put **`trailer_control.dll`, `trailer_control.ini` and `TrailerControl.exe`** in
   `...\steamapps\common\Euro Truck Simulator 2\bin\win_x64\plugins\`
   (create the `plugins` folder if it does not exist). The release's `...-plugins.zip` already has
   all three: just extract it inside that folder.
2. Start ETS2. The first time, an **"SDK features"** warning appears — accept it.
3. **Options → Controls**: find the **Trailer Control** device and bind its input to
   **"Attach / Detach trailer"**. Without it, truck uncoupling (joint 0) does nothing.

## 2. How to use

- In game, press **Ctrl + -** (the `-` key on the main keyboard or the numpad): the window opens.
- Click **Desengatar** / **Engatar** on the joint you want. The window hides and the game comes
  back — if the game paused while the window was in front, the command runs when you return.
- **Esc** or the same shortcut, with the window in front, takes you back to the game.
- **Configurações** (bottom of the window): changes the **shortcut that opens the window**. Click
  **Alterar…**, press the combination you want (e.g. Ctrl + F9) and **Salvar** — it applies
  immediately, no game restart. Other options live in `trailer_control.ini` (see "Troubleshooting").

> If ETS2 runs in **exclusive fullscreen**, it may minimize when the window appears. If that bothers
> you, use borderless/windowed fullscreen or keep the window on a second monitor.

## 3. Checklist

| # | Do | Expected | OK? |
|---|---|---|---|
| 1 | Get into the game with your truck | In the console (`~`): `[TrailerControl] Backend de memoria ativo para 1.62.3.3` | |
| 2 | Press **Ctrl + -** | The window opens in front showing **Jogo conectado** (game connected) | |
| 3 | Press **Esc** | Back to the game | |
| 4 | Take a **single** trailer job and open the window | Truck + Carreta 1 (trailer 1), joint 0 "Engatada" | |
| 5 | Stopped, click **Desengatar** on joint 0 | The window hides, the game comes back and drops the trailer | |
| 6 | Back up to it, open the window and click **Engatar** | The game couples it | |
| 7 | Take a **road train / double with dolly** (or B-double) job | The window shows every unit; joints 1, 2… with "estado lido da memória" (read from memory) | |
| 8 | Click **Diagnóstico** | `game.log.txt` gets a `=== Diagnostico de memoria ===` block with the trailers and `vtable ... OK` | |
| 9 | Stopped on flat ground, **Desengatar** joint 1 (dolly drawbar) | Dolly + rear trailer are loose; the window shows "Solta" | |
| 10 | Without moving the truck, **Engatar** joint 1 | Couples again (hitches less than 1.5 m apart) | |
| 11 | Drive a bit, stop and try **Engatar** on joint 1 with the trailers apart | The window refuses: "engates a X m (maximo 1.50 m)" | |
| 12 | On a B-double, do the same with joint 1 | Same as steps 9–10 | |
| 13 | In **Configurações**, change the shortcut (e.g. Ctrl + F9), save, go back and use it | The window opens with the new shortcut; the header shows "Esc ou Ctrl + F9" | |
| 14 | (Optional) In a **Convoy** session, repeat steps 9–10 | Works the same; note how other players see the loose trailer | |
| 15 | With a trailer coupled, turn on the turn signal (and hazards) | One beep per light flash; it stops when you turn the signal off | |
| 16 | In the window click **Som da seta** (the ✓ disappears) and turn the signal on | No sound; clicking again brings the beep back | |
| 17 | With no trailer coupled, turn the signal on | No beep | |
| 18 | Drag the volume bar (0–100) | A preview beep plays; the signal beep changes volume right away; 0 = muted | |
| 19 | Edit `volume` under `[som]` in the `.ini` while the game is running | Applies within 1 s, no restart | |

Also note anything **unexpected**: trailer "jumping", weird physics, freezes, trailers
disappearing, warnings when delivering the cargo, the window not showing over the game, etc.

## 4. How to report

Open an [issue](https://github.com/khenrique99/ETS2---Trailer-Control/issues) with:

1. The checklist above (or just the steps that failed) and the exact game version.
2. The `[TrailerControl]` lines from `Documents\Euro Truck Simulator 2\game.log.txt`
   (especially the **Diagnóstico** block).
3. The file `Documents\ETS2TrailerControl\status.json`, if something looks wrong in the window.
4. If the game closes by itself: what you clicked and, if present, the latest `crash.log` in
   `Documents\Euro Truck Simulator 2\`.

## 5. Troubleshooting

- **The game freezes/closes when uncoupling the dolly:** in `trailer_control.ini` (`plugins`
  folder) set `habilitado = 0` under `[memoria]`. Everything else keeps working.
- **Ctrl + - does not open the window:** check that `TrailerControl.exe` is in the `plugins`
  folder next to the DLL. You can also open the exe directly; it connects by itself. If something
  else in the game uses Ctrl + -, change the shortcut in Configurações.
- **You do not want internal uncoupling in Convoy:** set `permitir_convoy = 0` under `[memoria]`.
- **The window stays on "Aguardando o jogo" (waiting for the game):** is the DLL in
  `bin\win_x64\plugins`? Did you accept the SDK warning?
- **Internal joints "indisponível" (unavailable):** the message next to it gives the reason
  (different version, exe bytes do not match, TruckersMP, turned off in the `.ini`).
- **Coupling refused even with the trailers touching:** raise `distancia_max_engate_m` in the
  `.ini` (or `0` to skip the check) and report the value the window showed.
- **"Não consegui gravar" (could not save) in Configurações:** the game folder is protected; run
  `TrailerControl.exe` as administrator once, or edit `abrir_tela` in the `.ini` by hand.

## 6. Testing without the game (optional)

For those building the project: `scripts\build.ps1 -TestPackage` creates `build\...-teste.zip`.
Extract it and, in PowerShell inside it:

```powershell
.\teste-sem-jogo\fake_game.exe .\plugin\trailer_control.dll 60
```

This loads the plugin into a "fake" ETS2 (a parked road train) for 60 s. Open
`plugin\TrailerControl.exe` at the same time: it connects and joint 0's button drops/couples the
set. Internal joints are unavailable here (the memory backend only exists in the real game).

Automatic self-test: `.\teste-sem-jogo\fake_game.exe .\plugin\trailer_control.dll --selftest`
