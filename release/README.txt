DualSense for RDR2  v1.3.0-fork.2  -  Community Fork (based on OldKrab's v1.3.0)
==============================================================================

Unofficial community fork by TochaRude, adapted with the help of Antigravity
(not an official collaboration with OldKrab; full credit for the original mod belongs to OldKrab).
Testers and feedback are welcome: https://github.com/TochaRude/rdr2-dualsense/issues

Full DualSense support for Red Dead Redemption 2 on PC: adaptive triggers made per weapon,
voice-coil haptics, dialogue you can feel (experimental), lightbar, speaker and microphone - driven by what
the game is actually doing.


REQUIREMENTS
------------
- Red Dead Redemption 2 (PC, Steam / Rockstar / Epic), story mode
- Script Hook RDR2 + an ASI loader (dinput8.dll from Script Hook, or version.dll - either works)
  https://www.dev-c.com/rdr2/scripthookrdr2/
- A DualSense controller on a USB cable or Bluetooth (over Bluetooth everything but the controller's speaker
  and microphone). Plug it in before or after starting
  the game - both work.
- Close DSX / DS4Windows, and turn Steam Input OFF for RDR2
  (Steam -> RDR2 -> Properties -> Controller -> Disable Steam Input)
- Remove other DualSense mods (DualSenseHaptics.asi, NodixDualSense.asi ...) - they fight over the controller
- For the settings page and PlayStation button icons: Lenny's Mod Loader (LML), already installed (its vfs.asi next
  to RDR2.exe). Works with any game language (the page's texts are in English). Without LML everything
  else works; you change settings in DualSenseRDR2.ini instead.
- For voice commands: Windows' English (US) speech recognition. On a non-English Windows add it in
  Settings -> Time & language -> Speech. Without it voice commands are off; the whistle still works.


INSTALL
-------
Copy everything from this zip into the game folder - the folder with RDR2.exe in it, not a subfolder
(a file one folder too deep is simply never loaded):
    DualSenseRDR2.asi
    DualSenseRDR2\  (folder with DualSenseCore.dll, weapons.json, haptics.json, sound_labels.json, speech_voices.txt)
    lml\            (merges into LML's folder: lml\DualSenseRDR2 and one archive in lml\replace)
Start the game. Settings are created on first start in DualSenseRDR2.ini.
If you use LML's Mod Manager, make sure "DualSense for RDR2 - Settings page" is enabled.
Note: lml\replace\update_4\x64\data\ui\startup_data.rpf replaces a whole game archive (the menus) - if another
mod ships the same file, only one of the two can work. Remove other PlayStation button mods.

UNINSTALL: delete DualSenseRDR2.asi, the DualSenseRDR2 folder, DualSenseRDR2.ini, DualSenseRDR2.log,
lml\DualSenseRDR2 and the archive above.
UPDATE: copy the new files over the old ones - your DualSenseRDR2.ini is kept.

RED DEAD ONLINE: the mod is for story mode. Before playing Online, move the mod out of the game folder
(DualSenseRDR2.asi together with Script Hook, the ASI loader and LML, as for any mod) and move it back for
story mode. The mod changes none of the game's own files - nothing to repair afterwards.


WHAT YOU FEEL
-------------
Triggers   Every weapon has its own pull: take-up, wall, crisp break. The shot fires exactly when the trigger
           breaks. Single-action guns need their hammer / lever / bolt worked with R2 (follows the game's own
           "ready to shoot"). Recoil shoves back against your finger. Dirty guns feel gritty, exhaustion and
           cold make the pull shaky, express / explosive ammo kicks harder.
Dead Eye   Every target you paint ticks under your finger; each shot of the volley kicks as it really fires.
Fishing    R2 progressively loads the cast, pulses on release, and tugs when a fish bites (strike!).
           L2 - the rod grip - carries the real line tension and strains right before the line snaps. You feel
           the fish surge left / right, the reel click, and the catch flapping in your hand.
Lasso      Both L2 and R2 stiffen while holding a roped target, with rope tension, jerks and struggle vibration.
Bow        Heavy progressive resistance across R2 travel up to full 8/8 force at full draw, staged wood creaks,
           high-force arm strain after holding too long, early letdown vs. full arrow release.
Melee      Fists, knives and melee weapons have trigger feedback on swings, blocks and hits, with a heavier
           dual-trigger impact when landing a knockout or final blow.
Horse      Hoofbeats from the real hooves at every gait, on the real ground (mud, gravel, wood, grass, snow...),
           mounting and dismounting, jump landings, its heartbeat and breathing when it is spent, rearing.
On foot    Footsteps from Arthur's real feet on the real ground with high contrast across 9 surfaces (deeper and
           heavier when carrying a body or load, with periodic weight shifts), landings, wagon
           mounting/dismounting, ledge grabs, doors, drawing and holstering.
Water      Wading drags at every step - the deeper and the faster, the heavier; a splash as you go in.
Bushes     A rustle as you walk or ride into them, brushing while you move through.
Swimming   Every arm pull (left / right hand) and kick, the horse's paddling legs, a splash when you jump in.
Around you Hits felt from the direction they came from, nearby gunfire, low-health and wanted heartbeat,
           shivering cold, ground rumble and rail clacks when standing near moving trains, wagon ruts.
Explosions From where and how far they go off, with the real blast; close ones shake the whole controller and
           both triggers.
Weather    Rain and hail drops on the hat, storm/blizzard wind gusts, and distant thunder rolls.
Fights     While shots fly, steps, hooves and rain step back so the fight comes through.
Cutscenes  Steps, hooves, shots and hits are felt in cutscenes too.
Menus      Adaptive triggers stay completely relaxed and controller haptics/speaker output stay silent in main
           menus, pause menus, and loading screens.
Dialogue   (experimental, off by default) Every spoken line as the speaker's voice in your hands - see below.
Honor      A warm swell and gold glow when honor rises, a sinking thud when it falls.
Camp life  Bites, sips and puffs, cleaning your gun, brushing / feeding the horse, skinning, stowing pelts,
           multi-stage herb plucking (stem snap + root pull), bathing, crafting, cooking.
Table games Five finger fillet (every stab - and the miss), poker and blackjack chips and cards, dominoes.
Weapon wheel L2/R2 click like a selector switch while you browse.
Lightbar   Muzzle flash, hits, Dead Eye, lantern flicker, wanted police flash, low health, time of day.
           Player LEDs show rounds left when armed, appear for 3.5 s whenever health changes (PlayerLeds=auto|health|ammo|off),
           and animate a police-light sweep when wanted.
Speaker    Focused controller speaker audio (off by default: Sounds from the Controller): authentic in-game
           recordings (weapon draw / holster, lasso throw & rope jerks, bow creak, blade impacts & skinning)
           plus crisp mechanical clicks (cocking, lever / bolt / pump actions, live per-round reloads, dry fire,
           fishing reel clicks, weapon wheel & Dead Eye marks). Turning it off enforces a real hardware mute.


DIALOGUE HAPTICS (experimental, off by default)
-----------------------------------------------
Feel the voices: every spoken line in your hands - the speaker's own voice, its rhythm and tone, from the
side they stand on. Turn it on: Settings > Controls > Controller > DualSense > Dialogue Haptics.
  Dialogue Strength       how strong (60% to start)
  Dialogue Haptics In     Everywhere (cutscenes and conversations in the world) or Cutscenes only
  TV / Headphone Delay    how late your TV or headphones play the sound. Auto knows Bluetooth headphones -
                          raise it if you feel a line before you hear it.


VOICE AND WHISTLE (controller microphone, English, offline - the mute button pauses it)
Off by default: switch them on under Settings > Controls > Controller > DualSense (no restart needed).
---------------------------------------------------------------------------------------
  whistle                       call your horse
  "chao"                        Arthur greets the nearest person for real - counts for honor
  "easy" / "easy boy" / "steady"  calm or pat your horse
Say a command on its own, after a short pause - words in the middle of a conversation are ignored, so
talking to someone in the room won't set it off.
Words can be changed or switched off in DualSenseRDR2.ini, section [Voice] (leave a phrase empty to disable it).


SETTINGS
--------
In the game: Pause > Settings > Controls > Controller > DualSense - every setting of the mod, changed with
left / right like the game's own (strengths: 5% per press, hold to run). Changes apply as soon as you close
the menu. A setting's own options only show while it is On. On the title screen the page only tells you to
load your story - the mod starts with the game.
Everything is also in DualSenseRDR2.ini (every effect has its own strength, 0 = off); F9 - or holding the
PS button for a second - reloads the file in game.
Gyro aim (EXPERIMENTAL, off by default): [Gyro] Mode=aim turns on small gyro corrections while you aim
with L2. It is still being tuned and can feel floaty.
F10 shows the ground under you; F10 again while shown overrides how it feels.
Debug=1 writes details to DualSenseRDR2.log - attach it when reporting a problem.


CREDITS
-------
Game research: alloc8or's RDR3 native database, femga's rdr3_discoveries, Halen84 and others' decompiled
script references. nlohmann/json (MIT). stb_vorbis by Sean Barrett (public domain). Script Hook RDR2 by
Alexander Blade. Haptics over Bluetooth: the report format worked out by SAxense (https://apps.sdore.me/SAxense).
Source code (MIT): see the mod page.


CHANGES
-------
1.3.0-fork.2  Stability, exit-crash, and save-load reload fixes:
              - Exit crash fix (0xc0000409 / 0x40000015): process-lifetime singletons and background
                thread slots remain immortal across ExitProcess instead of running CRT atexit destructors
                on active threads, and shv_scriptUnregister is skipped on process exit.
              - ScriptHookRDR2 V2 save-load hot-reload fix: split thin CRT-free DualSenseRDR2.asi loader
                shim from pinned DualSenseRDR2\DualSenseCore.dll so FreeLibrary/LoadLibrary reloads drop
                the .asi refcount to 0 cleanly, re-register script_main (#2+), and resume haptics/triggers
                immediately while XInput hooks and string-table nodes stay intact.
              - Save-load hardening & diagnostics: skip entity/UI/world natives during loading screens and
                player switches, reset stale per-session handles on save loads, verify and rebuild
                ScriptDatastoreRoot.DualSenseSettings automatically, log script thread starts/stalls, and
                pass native game rumble through if the script thread ever stalls during active gameplay.
1.3.0-fork.1  Community fork release based on OldKrab's v1.3.0 (adapted with Antigravity):
              Controller Speaker: enforced real HID hardware speaker mute + audio-thread voice purge when
              Mechanics=0; focused speaker output strictly on authentic in-game RDR2 .awc recordings (hip-holster
              weapon draw/holster, lasso, bow creak, blade/skinning) and crisp mechanical clicks (cocking,
              lever/bolt/pump, live per-round reloads, dry fire, reel clicks, wheel tick, Dead Eye marks).
              Triggers & Combat: heavy progressive bow resistance (FEEDBACK up to 8/8 torque + staged creaks
              and arm strain), break-aligned dry click, KickStrength=0 recoil off + auto-align safety clamp,
              dual-trigger L2+R2 lasso struggle, smooth progressive R2 fishing cast + release pulse, melee on
              CIRCLE + KO finishers (R1 kept free for Take Cover & Horse Brake), Double-Action variants break
              at zone 8, WEAPON_OBJECT / Electric Lantern filtering, and completely relaxed/silent triggers &
              haptics in main menus, pause menus, and loading screens.
              World & LEDs: high-contrast tuning across all 9 ground surfaces + 5.5 cm slow-walk sensitivity,
              state-driven horse/wagon mounting (excluding trains/boats), event-driven multi-stage herb
              plucking, nearby moving train rumble, hail/gust/thunder weather, carrying load weight/grip,
              on-change 3.5 s Health Player LEDs, explicit PlayerLeds=off clearing, and wanted police-light
              LED sweep + tension heartbeat.
              Fixes: async Win32 DllMain boot, native right-stick deadzone preserved with clean still-only L2
              gyro bias/free-aim gating, clean overlapped HID cancellation, Loader-Lock-safe unload handling,
              thread-safe speaker/gameaudio locking, immediate log flush, and dynamic 0x05 HID calibration.
1.3.0  Haptics over Bluetooth: everything you feel over USB now works without the cable too (not the
       controller's speaker and microphone). Bluetooth=0 in the ini goes back to the plain rumble motors -
       then without the rain, bush and wading textures, which only blurred into one endless rumble there.
       No more phantom hits after a fight: health slowly draining (a tonic wearing off) was felt as a bullet
       hit every few seconds - a hit is now only what the game reports as damage.
1.2.1  Swimming: every arm pull and kick is felt, left and right; the horse's paddling legs when it swims;
       a splash when you jump into water.
       Sounds from the Controller Off now also silences the gun draw / holster sounds.
1.2.0  Snow: no more constant vibration; steps and hooves in deep snow are felt.
       Hoofbeats only from the real hooves - nothing generated - and steadier at a gallop; every step of both
       feet is felt (the left foot could go silent).
       Pause menu and loading screens: all vibration stops.
       Water and mud: wading drags harder the deeper and faster you go, with a splash going in; every ground
       has its own feel.
       Bushes: a rustle as you walk or ride through them.
       Explosions: felt from where and how far they go off, with the real blast; close ones shake the triggers.
       Rain on by default (30%). In fights, steps, hooves and rain step back so shots and hits come through.
       New defaults: Sounds from the Controller Off, Game's Own Rumble 15% (less buzzing near plants),
       Rain 30% - applied to your ini unless you had changed them.
       Hold PS for a second to reload the settings (like F9). Whistling while aiming no longer moves the gun.
1.1.0  Dialogue haptics (experimental, off by default).
       Steps, hooves, shots and hits are felt in cutscenes too.
       Controller plugged in after the game started (or pulled out and back) works within a second -
       controls and haptics.
       Settings page: the selected row is highlighted like the game's own pages, a setting's own options only
       show while it is On, experimental features have their own sections.
1.0.0  First release.
