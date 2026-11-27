DualSense for RDR2  v2.0.1-ds4-exp.1  -  Experimental DualShock 4 Edition (by OldKrab & TochaRude)
====================================================================================================

EXPERIMENTAL RELEASE FOR SONY DUALSHOCK 4 (PS4) CONTROLLERS (USB, BLUETOOTH & WIRELESS ADAPTOR)
Note: DualShock 4 hardware does not include adaptive triggers or voice-coil HD actuators, so
trigger resistance and voice-coil audio haptics are automatically disabled / emulated via the
DualShock 4's dual rumble motors.


SUPPORTED HARDWARE & REQUIREMENTS
---------------------------------
- Red Dead Redemption 2 (PC, Steam / Rockstar / Epic), story mode
- A Sony DualShock 4 controller (v1 CUH-ZCT1, v2 CUH-ZCT2, or Sony DS4 USB Wireless Adaptor)
  via USB cable or Bluetooth
- ScriptHookRDR2 V2 (recommended: https://www.nexusmods.com/reddeadredemption2/mods/1472) or classic
  Script Hook RDR2 + an ASI loader (dinput8.dll or version.dll): https://www.dev-c.com/rdr2/scripthookrdr2/
- Close DS4Windows / DSX / reWASD, and turn Steam Input OFF for RDR2
  (Steam -> RDR2 -> Properties -> Controller -> Disable Steam Input)
- Remove other DualSense / DS4 controller mods - they fight over the controller
- For the settings page and PlayStation button icons: Lenny's Mod Loader (LML,
  https://www.rdr2mods.com/downloads/rdr2/tools/76-lennys-mod-loader-rdr/), already installed (its vfs.asi
  next to RDR2.exe). Automatically translates into English, German, French, Spanish, Italian, Portuguese,
  Polish, and Russian (falling back to English for other game languages). Without LML everything else works;
  you change settings in DualSenseRDR2.ini instead.


WHAT WORKS ON DUALSHOCK 4
-------------------------
- Native Controller & XInput Hook: Plug in via USB or Bluetooth anytime (even mid-game). Raw analog L2/R2
  triggers pass cleanly through to RDR2 without interference.
- PlayStation Button Icons & 8-Language Menu: Full in-game settings page under
  Pause > Settings > Controls > Controller > DualSense (63 options, auto-translated).
- Enhanced Dual-Motor Rumble Emulation: Translates the mod's 186 haptic recipes (footsteps by surface,
  horse hooves, gunshots, bow draw, lasso struggle, weather, explosions, directional hits, cutscenes, etc.)
  plus the game's own native rumble cleanly onto the DS4's left/right rumble motors.
- Full Reactive Lightbar: Muzzle flash, hits, Dead Eye, Eagle Eye, lit fuses, carried lantern, wanted
  police lights, low health, honor glow, custom idle colors (Time of Day, Off, presets, or #RRGGBB),
  brightness scaling, and clean automatic shutoff in menus and on exit.
- Gyro Aim (Experimental): Uses the DualShock 4's factory-calibrated 6-axis IMU + stillness bias learning.
- Touchpad Click, 4-Way Swipes & Two-Finger Tap: All 6 gestures supported with Calm/Pat Horse (L3),
  Eagle Eye (L3+R3), Open Map, Open Satchel, Open Journal, Cinematic Camera, Long Whistle, and more.


HARDWARE LIMITATIONS (NOT AVAILABLE ON DUALSHOCK 4)
---------------------------------------------------
- No Adaptive Triggers (PS5 DualSense hardware only)
- No Voice-Coil HD Audio Haptics or Dialogue/Game-Audio HD playback
- No 5 Player LEDs below the touchpad (PS5 hardware only)
- No Mute Button / Orange Mute LED (PS5 hardware only)
- Controller Speaker & Whistle/Voice Microphone commands are disabled in this experimental DS4 build


INSTALL
-------
Copy everything from this zip into the game folder - the folder with RDR2.exe in it, not a subfolder:
    DualSenseRDR2.asi
    DualSenseRDR2\  (folder with DualSenseCore.dll, weapons.json, haptics.json, sound_labels.json,
                     speech_voices.txt, translations.json)
    lml\            (merges into LML's folder: lml\DualSenseRDR2 and one archive in lml\replace)
Start the game. Settings are created on first start in DualSenseRDR2.ini.
If you use LML's Mod Manager, make sure "DualSense for RDR2 - Settings page" is enabled.

RED DEAD ONLINE: the mod is for story mode. Move mod files out of the game folder before playing Online.

Source code (MIT): https://github.com/TochaRude/rdr2-dualsense
Original DualSense RDR2 mod by OldKrab; Community Edition & Experimental DS4 build by TochaRude.
