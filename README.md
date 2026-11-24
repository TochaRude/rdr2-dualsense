# DualSense for RDR2 (Community Fork)

Full DualSense support for Red Dead Redemption 2 on PC — adaptive triggers per weapon, voice-coil haptics,
dialogue you can feel (experimental), lightbar, player LEDs, controller speaker audio, and microphone (beta),
driven by what the game is actually doing (ScriptHookRDR2 `.asi` mod).

> **About This Fork**  
> This repository is an **independent community fork** of [OldKrab/rdr2-dualsense](https://github.com/OldKrab/rdr2-dualsense) ([Nexus Mods](https://www.nexusmods.com/reddeadredemption2/mods/11444)), originally created by **OldKrab**.  
> The adjustments and fixes in this fork were developed by **TochaRude** with the help of **Antigravity**.  
> This fork is **not** an official collaboration with OldKrab — full credit for the original architecture and core mod belongs to the original author.  
> **Testers and feedback are very welcome!** Please report bugs, controller quirks, or suggestions via [GitHub Issues](https://github.com/TochaRude/rdr2-dualsense/issues).

Players: download the latest build from [GitHub Releases](https://github.com/TochaRude/rdr2-dualsense/releases/latest)
and follow the installation steps in `README.txt` inside the archive.

---

## Versioning Convention (`<upstream>-fork.<N>`)

Because this fork diverges from OldKrab's official releases without being part of the upstream release train, versions follow `<upstream-base>-fork.<N>`:
- **`v1.3.0-fork.1`**: Based on OldKrab's original **`v1.3.0`**, Fork Release **`1`**.
- **`v1.3.0-fork.2`**: Exit-crash fix, **ScriptHookRDR2 V2** save-load hot-reload architecture, and `DualSenseSettings` recovery.
- Future fork releases built on the same base increment the suffix (`v1.3.0-fork.3`, `v1.3.0-fork.4`, ...). If changes from a newer upstream version are merged later, the upstream prefix updates accordingly. This guarantees fork releases never collide with official upstream version numbers.

---

## Differences from the Original Mod

### 1. Focused Controller Speaker Audio & True Hardware Mute (`[Speaker]`)
- **Strict Hardware Mute When Disabled:** Setting `Mechanics=0` (*"Sounds from the Controller"* off) now enforces a real HID hardware speaker mute (`p[7] = 0x00`, `p[5] = 0x00`) and purges queued/playing speaker voices safely on the audio thread — zero controller speaker output leaks through when disabled.
- **Real RDR2 Recordings & Crisp Mechanical Clicks Only:** Speaker playback is strictly limited to authentic in-game RDR2 `.awc` recordings (`gun_draw` / `gun_holster` restricted to hip-holstered sidearms, `lasso_throw`, `rope_jerk`, `rope_catch`, `bow_creak`, `impact_blade`, `impact_blade_deep`, `skin_cut`), sharp firearm & fishing-reel mechanical clicks (`hammer_cock*`, `lever_*`, `bolt_*`, `pump_*`, `slide_small_*`, `reload_*`, `stripper_clip`, `mag_*`, `slide_release`, `topbreak_*`, `breech_*`, `cylinder_gate`, `dry_fire`, `reel_click`), and minimal Dead Eye / weapon-wheel UI clicks (`deadeye_mark`, `wheel_tick`).
- **Live Per-Round Reloads:** Synchronizes opening the cylinder gate / breech / bolt, each inserted cartridge or shell (`GET_AMMO_IN_CLIP`), and closing the action / slide release when rounds were actually loaded. Noisy continuous footsteps, bushes, campfire crackling, UI menus, and synthetic chatter tones are kept off the speaker.

### 2. Adaptive Triggers, Combat & Menu Behavior
- **Silent & Relaxed Menus / Loading Screens:** Adaptive triggers stay completely relaxed (`fx::off()`) and all pending haptic/speaker cues (as well as wheel `L1` detection, dialogue tracks, and rumble) are suppressed while on the title screen, in loading screens, or in pause menus (`!g.valid || g.menu || frozen`).
- **Heavy Progressive Bow Draw:** Solid `FEEDBACK (0x21)` trigger resistance ramping cleanly across travel up to full `8/8` hardware torque at full draw, staged wood/string creak haptics at 50% and 95% draw, arm-strain modulation, and distinct early letdown vs. full arrow release.
- **Dynamic Dry Click, Recoil Off & Auto-Align Safety:** Pulling `R2` on an empty chamber produces a light, crisp dry-click (`fx::weapon`) aligned to each weapon's configured `break_zone_`; `Triggers.KickStrength=0` cleanly disables trigger recoil completely, and auto-align shot points are clamped to `[30, 254]`.
- **Dual-Trigger Lasso Struggle:** Holding a roped target stiffens **both** `L2` and `R2`, transitioning into struggle vibration during pulls and jerks.
- **Progressive Fishing Cast:** `R2` progressively builds cast tension (`state 2/3`) smoothly across both stages, followed by a crisp release pulse (`state 4`).
- **Melee (`CIRCLE`) & Knockout Finishers + Riding Fix:** In-game melee triggers exclusively on `CIRCLE` (`R1` stays completely free for Take Cover on foot and Horse Brake when riding) with exact delta hit/KO confirmation (`melee_ko`), heavier dual-trigger knockout impacts, and Double-Action revolver variants (`Micah`, `Granger`, `Algernon`) tuned to `break_zone: 8`.
- **Non-Weapon & Lantern Filtering:** Filters `WEAPON_OBJECT` (`joaat("WEAPON_OBJECT")`) and ungrouped items so carrying non-weapon objects never triggers false firearm effects, and maps `WEAPON_MELEE_DAVY_LANTERN` cleanly as a `"tool"`.

### 3. Refined World, Weather & Movement Haptics
- **High-Contrast 9 Ground Surfaces + Slow-Walk Sensitivity:** Re-tuned frequency, envelope, and multi-pulse profiles across all 9 ground materials (`ground`, `grass`, `mud`, `stone`, `gravel`, `wood`, `snow`, `sand`, `water`) for footsteps and hooves, with a refined `5.5 cm` touchdown threshold and sprint-swing decay for slow camp walking.
- **State-Driven Horse & Wagon Mounting:** Saddle mount (`horse_mount`), wagon mount (`wagon_mount`), and dismounts driven directly by ped/mount/wagon state transitions (excluding trains and boats, and safely reset across halts/loads).
- **Event-Driven Herb Plucking (`pluck`):** Multi-stage stem-snap and root-pull tactile haptics triggered cleanly from herb loot events (`EVENT_CALCULATE_LOOT` / `EVENT_LOOT` `slot(29) != 0` with `1.8 s` debounce — no container/loot false positives).
- **Nearby Moving Trains & Expanded Weather:** Directional ground rumble and rail clacks near moving trains (`train_pass`), plus hail/sleet (`hail_drop`), storm/blizzard wind gusts (`wind_gust` including `THUNDER`, `GROUNDBLIZZARD`, `WHITEOUT`), and distant thunder (`thunder_roll`) via `GET_CURR_WEATHER_STATE`.
- **Carrying Bodies & Heavy Loads:** Deeper footsteps (`IS_PED_CARRYING_SOMETHING`), shoulder weight shifts (`carry_shift`), and subtle unarmed `L2`/`R2` grip weight (`!g.armed`).

### 4. Player LEDs & Wanted System (`[Lightbar]`)
- **On-Change Health Player LEDs (`PlayerLeds=auto|health|ammo|off`):** Displays remaining clip rounds when armed (`auto`), lights up the 5 Player LEDs for 3.5 seconds when Arthur's health changes by `>= 2%` while unarmed, and actively clears LEDs to `0` when set to `off` (and keeps disabled lightbar off after flashes).
- **Wanted Police Sweep & Escalating Heartbeat:** Alternating police-light Player LED sweep, red/blue lightbar flashes, and a tension heartbeat that speeds up with `GET_WANTED_SCORE`.

### 5. Stability, Gyro/Camera & Hardware Fixes
- **Zero Unwanted L2 Camera Pull:** Preserves RDR2's native right-stick deadzone, enforces case-insensitive `Gyro.Mode` parsing (`aim` / `always`), armed-only free-aim gating, still-only initial gyro bias seeding, no drift learning during active aim, and menu/loading gating for `Gyro.Mode=always`.
- **Async Startup, Thread Safety & Clean Shutdown:** Moves heavy JSON/audio boot onto an async Win32 `CreateThread`, uses immediate `fflush` on `INFO`/`WARN`/`ERROR` logs, enforces thread-safe audio-thread speaker purging, clean overlapped HID cancellation (`CancelIoEx`), immortal process-lifetime singletons (preventing `0xc0000409` / `0x40000015` CRT `atexit` exit crashes), and `173/173` passing offline selftests plus automated Win32 hot-reload verification (`dsr2_reload_test.exe`).
- **ScriptHookRDR2 V2 Save-Load Hot-Reload & Datastore Recovery:** Splits a thin, CRT-free, thread-free `DualSenseRDR2.asi` loader shim from the pinned `DualSenseRDR2\DualSenseCore.dll` so `FreeLibrary`/`LoadLibrary` save-load cycles unload and re-register cleanly while keeping XInput hooks, injected strings, and worker threads intact; resets transient entity handles across loads, rebuilds `ScriptDatastoreRoot.DualSenseSettings`, and passes native game rumble through if a script thread ever stalls during gameplay.
- **DualSense HID & WASAPI Precision:** Dynamic HID feature report `0x05` length (64-byte USB / 41-byte Bluetooth) with strict gyro calibration range validation, USB/BT endpoint handling, 24/32-bit PCM support, and a lock-free SPSC ring buffer.

---

## Repository layout
| Path | What |
|---|---|
| `mod/` | The mod (C++20, CMake + MinGW-w64). `mod/README.md`: build, install, feature table. |
| `mod/src/` | Controller I/O (HID, WASAPI haptics & speaker), trigger engine, game state (natives), features, voice/whistle. |
| `tools/trigger_lab/` | Python lab to design and test trigger effects and haptic recipes outside the game; `data/` holds `weapons.json` and `haptics.json` (`171 recipes`). |
| `tools/awc/` | Game sound bank tools (AWC decoding, sound / material name recovery, speech voice table) and read-only live-memory research scripts. |
| `tools/dev/` | Release script, ini updater, one-off patch scripts. |
| `release/` | Player `README.txt` and mod page text. |

## Build
```powershell
cmake -S mod -B mod/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build mod/build
.\mod\build\dsr2_test.exe --selftest
.\mod\build\test_reload\dsr2_reload_test.exe
powershell -ExecutionPolicy Bypass -File tools/dev/make_release.ps1
```

## License
MIT — see `LICENSE`. Uses nlohmann/json (MIT) and stb_vorbis (public domain). Full credit for the original DualSense RDR2 mod goes to [OldKrab](https://github.com/OldKrab/rdr2-dualsense).
