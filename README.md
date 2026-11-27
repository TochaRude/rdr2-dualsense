# DualSense for RDR2 (Community Fork)

Full DualSense support for Red Dead Redemption 2 on PC — adaptive triggers per weapon, voice-coil haptics,
dialogue you can feel (experimental), lightbar, player LEDs, touchpad gestures, controller speaker audio,
8-language in-game settings menu, and microphone (beta), driven by what the game is actually doing
(ScriptHookRDR2 `.asi` mod).

> **About This Fork / Continuation**  
> This repository began as an independent community fork of [OldKrab/rdr2-dualsense](https://github.com/OldKrab/rdr2-dualsense) ([original Nexus Mods page](https://www.nexusmods.com/reddeadredemption2/mods/11444)), originally created by **OldKrab**.  
> With **OldKrab's blessing**, development and the new **[DualSense for RDR2 - Reworked](https://www.nexusmods.com/reddeadredemption2/mods/11933)** Nexus Mods release are now continued independently here by **TochaRude** (built with the help of **Antigravity**). Full credit for the original architecture and core mod belongs to **OldKrab**.  
> **Testers and feedback are very welcome!** Please report bugs, controller quirks, or suggestions via [GitHub Issues](https://github.com/TochaRude/rdr2-dualsense/issues) or [Nexus Mods](https://www.nexusmods.com/reddeadredemption2/mods/11933).

Players: download the latest release from **[Nexus Mods](https://www.nexusmods.com/reddeadredemption2/mods/11933)** or **[GitHub Releases](https://github.com/TochaRude/rdr2-dualsense/releases)**
and follow the installation steps in `README.txt` inside the archive.

---

## Versioning

Starting with **`v2.0.0`**, this repository publishes standalone releases directly from `main`:
- **`v1.3.0-fork.1` – `v1.3.0-fork.3`**: Earlier community fork releases built on OldKrab's `v1.3.0`.
- **`v1.4.0-fork.1-pre.1`** *(Pre-Release)*: Combined upstream `v1.4.0` (`24535bd`) with expanded 63-row 8-language settings UI, touchpad gestures, full-travel actions, 93-weapon coverage, world haptics, and `.awc` soundbank effects.
- **`v2.0.0`**: First full standalone release on `main`, adding native-reviewed Russian localization, a clean DualSense hardware reset when quitting RDR2 (turning off lightbar, LEDs, triggers, rumble, speaker route, and releasing the HID handle cleanly), and updated end-user documentation.

---

## Differences from the Original Mod

### 1. Expanded In-Game Settings Menu, 8 Languages & Touchpad Gestures
- **Expanded In-Game DualSense Settings Page (`27 -> 63` options across `7` categories):** Rebuilt `startup_data.rpf` (`DUALSENSE_SETTINGS`) with live dependency bindings (`SHOW_IF` / `ACTIVE_IF`), adding a global **Trigger Strength** slider (`[Triggers] Strength`, `0–200%`) and a dedicated **Touchpad** category (`Click`, `SwipeUp`, `SwipeDown`, `SwipeLeft`, `SwipeRight`, `TwoFingerTap` across 15 presets: `Off`, Map/Camera, Whistle, Weapon Wheel, Eagle Eye, Pause, etc.).
- **8-Language In-Game UI with Automatic English Fallback (`translations.json`):** Out-of-the-box support for **English (`en`), German (`de`), French (`fr`), Spanish (`es`, Spain & LATAM), Italian (`it`), Portuguese (`pt`), Polish (`pl`), and Russian (`ru`)**, detected automatically via `GET_CURRENT_LANGUAGE` as well as on the title screen from RDR2's loaded `global.yldb` string table (optionally overridable via `[General] Language=auto|en|de|fr|es|it|pt|pl|ru`), with automatic **English** fallback for unsupported game languages.

### 2. Focused Controller Speaker Audio, Soundbank Haptics & True Hardware Mute (`[Speaker]`)
- **Strict Hardware Mute When Disabled:** Setting `Mechanics=0` (*"Sounds from the Controller"* off) now enforces a real HID hardware speaker mute (`p[7] = 0x00`, `p[5] = 0x00`) and purges queued/playing speaker voices safely on the audio thread — zero controller speaker output leaks through when disabled.
- **Real RDR2 Recordings & Crisp Mechanical Clicks Only:** Speaker playback is strictly limited to authentic in-game RDR2 `.awc` recordings (`gun_draw` / `gun_holster` restricted to hip-holstered sidearms, `lasso_throw`, `rope_jerk`, `rope_catch`, `bow_creak`, `impact_blade`, `impact_blade_deep`, `skin_cut`), sharp firearm & fishing-reel mechanical clicks (`hammer_cock*`, `lever_*`, `bolt_*`, `pump_*`, `slide_small_*`, `reload_*`, `stripper_clip`, `mag_*`, `slide_release`, `topbreak_*`, `breech_*`, `cylinder_gate`, `dry_fire`, `reel_click`), and minimal Dead Eye / weapon-wheel UI clicks (`deadeye_mark`, `wheel_tick`), cleanly separated via `Speaker.Ambient` and `Speaker.UI`.
- **Authentic RDR2 `.awc` Soundbank Haptics & Lasso Loop Fix:** Genuine RDR2 `.awc` recordings for footsteps/hooves/wading/bushes (`FEET_MATERIALS`, `WATER`), weapon draw/holster (`MELEE`), thunder (`WEATHER.THUNDER`), and UI clicks (`FRONTEND`) with dedicated DSP presets (`surface`, `drop`, `thunder`), plus an extended `0.65 s` lasso twirl cadence and voice-tag deduplication (`AudioEngine::Voice::tag`) preventing overlapping/doubled lasso sounds.
- **Live Per-Round Reloads:** Synchronizes opening the cylinder gate / breech / bolt, each inserted cartridge or shell (`GET_AMMO_IN_CLIP`), and closing the action / slide release when rounds were actually loaded. Noisy continuous footsteps, bushes, campfire crackling, and synthetic chatter tones are kept off the speaker.

### 3. Adaptive Triggers, Full-Travel Actions, 93 Weapons & Combat
- **Global Trigger Strength (`[Triggers] Strength`, `0–200%`):** Centrally scales all adaptive trigger resistances (`FEEDBACK`, `VIBRATION`, `WEAPON`, `simple_weapon`) directly in-game and in `DualSenseRDR2.ini`.
- **Full-Travel Lever, Bolt & Pump Cycling (`break_zone: 9`) & Refined Double-Action:** Tactile mid-stroke `detent` (`40 ms`), state-aware cycling resistance, and optional dirty-gun slips (`Triggers.HammerSlip`) extended to lever, bolt, and pump actions (not just revolver hammers); refined Double-Action revolver cylinder indexing halfway through the pull plus realistically stiffer pull weight when dirty.
- **Complete 93-Weapon RDR2 Coverage (`64 -> 93` weapons):** Covers every Story & RDO weapon including unique variants (`WEAPON_PISTOL_MAUSER_DRUNK` / Midnight's Pistol, `WEAPON_REVOLVER_CATTLEMAN_PIG` / Granger's Revolver, `WEAPON_REVOLVER_CATTLEMAN_JOHN`, `WEAPON_REVOLVER_NAVY_CROSSOVER`, `WEAPON_REVOLVER_DOUBLEACTION_GAMBLER`), all Story/RDO knives, heavy axes/cleavers/machetes (`impact_chop`), hammer (`impact_blunt`), Homing Tomahawk, RDO bolas variants, Metal Detector, and Advanced Camera.
- **Quick-Draw / Duel R2 Tension:** While holstered on foot, slowly half-pulling `R2` builds smooth progressive tension up to a crisp ~70% wall (`zone 7`) with subtle charge pulses (`quickdraw_tension`) and a break click — keeping full analog `R2` passthrough (`game_rt = -1`).
- **Silent & Relaxed Menus / Loading Screens + Gated Confirm:** Adaptive triggers stay completely relaxed (`fx::off()`) and pending haptic/speaker cues are suppressed on the title screen, in loading screens, or in pause menus (`!g.valid || g.menu || frozen`), while Cross confirm (`ui_confirm`) fires strictly on the loaded main menu or pause menu (never during the intro or loading screens).
- **Heavy Progressive Bow Draw:** Solid `FEEDBACK (0x21)` trigger resistance ramping cleanly across travel up to full `8/8` hardware torque at full draw, staged wood/string creak haptics at 50% and 95% draw, arm-strain modulation, and distinct early letdown vs. full arrow release.
- **Dynamic Dry Click, Recoil Off & Auto-Align Safety:** Pulling `R2` on an empty chamber produces a light, crisp dry-click (`fx::weapon`) aligned to each weapon's configured `break_zone_`; `Triggers.KickStrength=0` cleanly disables trigger recoil completely, and auto-align shot points are clamped to `[30, 254]`.
- **Dual-Trigger Lasso Struggle & Progressive Fishing Cast:** Holding a roped target stiffens **both** `L2` and `R2` into struggle vibration during pulls and jerks; `R2` progressively builds fishing cast tension (`state 2/3`) followed by a crisp release pulse (`state 4`).
- **Melee (`CIRCLE`) & Knockout Finishers + Riding Fix:** In-game melee triggers exclusively on `CIRCLE` (`R1` stays completely free for Take Cover on foot and Horse Brake when riding, and `R2` stays completely slack on `CIRCLE`) with exact delta hit/KO confirmation (`melee_ko`) and heavier dual-trigger knockout impacts.
- **Non-Weapon & Lantern Filtering:** Filters `WEAPON_OBJECT` (`joaat("WEAPON_OBJECT")`) and ungrouped items so carrying non-weapon objects never triggers false firearm effects, and maps `WEAPON_MELEE_DAVY_LANTERN` cleanly as a `"tool"`.

### 4. Refined World, Weather & Movement Haptics
- **High-Contrast 9 Ground Surfaces + Slow-Walk Sensitivity:** Re-tuned frequency, envelope, and multi-pulse profiles across all 9 ground materials (`ground`, `grass`, `mud`, `stone`, `gravel`, `wood`, `snow`, `sand`, `water`) for footsteps and hooves, with a refined `5.5 cm` touchdown threshold and sprint-swing decay for slow camp walking.
- **Lit Fuses, Eagle Eye & Progressive Gun Cleaning:** Right-biased spark crackle (`fuse_crackle`) and escalating warm-orange-to-red lightbar flicker on lit Dynamite & Fire Bottles; distinct `eagle_eye_in` / `eagle_eye_out` focus pulse with icy blue-white lightbar glow; and gritty-to-silky progressive barrel scrubbing (`cloth_rub_gritty` -> `cloth_rub` + gleam click) as weapon dirt drops.
- **Boat Rowing, Spooked Horse & Wagon Braking:** Real two-hand oar stroke detection (`oar_pull`) with turn panning plus gentle hull wave lap (`boat_wave`), nervous rein tugs (`horse_spook`) + accelerated horse heartbeat when agitated, and surface-scaled braking judder (`wagon_brake`) when braking hard in wagons/coaches.
- **State-Driven Horse & Wagon Mounting:** Saddle mount (`horse_mount`), wagon mount (`wagon_mount`), and dismounts driven directly by ped/mount/wagon state transitions (excluding trains and boats, and safely reset across halts/loads).
- **Event-Driven Herb Plucking (`pluck`):** Multi-stage stem-snap and root-pull tactile haptics triggered cleanly from herb loot events (`EVENT_CALCULATE_LOOT` / `EVENT_LOOT` `slot(29) != 0` with `1.8 s` debounce — no container/loot false positives).
- **Nearby Moving Trains, Expanded Weather & Carrying Loads:** Directional ground rumble and rail clacks near moving trains (`train_pass`), hail/sleet (`hail_drop`), storm/blizzard wind gusts (`wind_gust` including `THUNDER`, `GROUNDBLIZZARD`, `WHITEOUT`), distant thunder (`thunder_roll`) via `GET_CURR_WEATHER_STATE`, deeper footsteps (`IS_PED_CARRYING_SOMETHING`), shoulder weight shifts (`carry_shift`), and subtle unarmed `L2`/`R2` grip weight (`!g.armed`).

### 5. Player LEDs & Wanted System (`[Lightbar]`)
- **On-Change Health Player LEDs (`PlayerLeds=auto|health|ammo|off`):** Displays remaining clip rounds when armed (`auto`), lights up the 5 Player LEDs for 3.5 seconds when Arthur's health changes by `>= 2%` while unarmed, and actively clears LEDs to `0` when set to `off` (and keeps disabled lightbar off after flashes).
- **Wanted Police Sweep & Escalating Heartbeat:** Alternating police-light Player LED sweep, red/blue lightbar flashes, and a tension heartbeat that speeds up with `GET_WANTED_SCORE`.

### 6. Stability, Gyro/Camera & Hardware Fixes
- **Zero Unwanted L2 Camera Pull & Drift-Free Gyro:** Preserves RDR2's native right-stick deadzone, enforces case-insensitive `Gyro.Mode` parsing (`aim` / `always`), armed-only free-aim gating, variance-bounded stillness-only bias learning (`[-5, +5] deg/s`), smooth `0.8 deg/s` soft deadzone, menu/loading gating for `Gyro.Mode=always`, and independent camera yaw/pitch writes with a `0.40 s` post-shot pitch recoil mute in `Output=camera`.
- **Async Startup, Clean Exit Reset & Thread Safety:** Moves heavy JSON/audio boot onto an async Win32 `CreateThread`, uses immediate `fflush` on `INFO`/`WARN`/`ERROR` logs, enforces thread-safe audio-thread speaker purging, executes a Loader-Lock-safe final HID reset (`DLL_PROCESS_DETACH` -> `reset_on_exit()`, clearing lightbar, player LEDs, mute LED, triggers, rumble, and speaker routing before closing the handle), prevents `0xc0000409` / `0x40000015` CRT `atexit` exit crashes, and passes offline selftests (`dsr2_test.exe --selftest`) plus automated Win32 hot-reload verification (`dsr2_reload_test.exe`).
- **ScriptHookRDR2 V2 Save-Load Hot-Reload & Datastore Recovery:** Splits a thin, CRT-free, thread-free `DualSenseRDR2.asi` loader shim from the pinned `DualSenseRDR2\DualSenseCore.dll` so `FreeLibrary`/`LoadLibrary` save-load cycles unload and re-register cleanly while keeping XInput hooks, injected strings, and worker threads intact; resets transient entity handles across loads, rebuilds `ScriptDatastoreRoot.DualSenseSettings`, and passes native game rumble through if a script thread ever stalls.
- **DualSense HID & WASAPI Precision:** Dynamic HID feature report `0x05` length (64-byte USB / 41-byte Bluetooth) with strict gyro calibration range validation, USB/BT endpoint handling, 24/32-bit PCM support, and a lock-free SPSC ring buffer.

---

## Repository layout
| Path | What |
|---|---|
| `mod/` | The mod (C++20, CMake + MinGW-w64). `mod/README.md`: build, install, feature table. |
| `mod/src/` | Controller I/O (HID, WASAPI haptics & speaker), trigger engine, game state (natives), features, voice/whistle. |
| `tools/trigger_lab/` | Python lab to design and test trigger effects and haptic recipes outside the game; `data/` holds `weapons.json` (`93 weapons`), `haptics.json` (`186 recipes`), and `translations.json` (`8 languages`). |
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
