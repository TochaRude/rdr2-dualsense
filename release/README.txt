DualSense for RDR2  v2.0.0  -  Community Edition (by OldKrab, continued by TochaRude)
=================================================================================

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
  to RDR2.exe). Automatically translates into English, German, French, Spanish, Italian, Portuguese, Polish,
  and Russian (falling back to English for other game languages). Without LML everything else works; you
  change settings in DualSenseRDR2.ini instead.
- For voice commands: Windows' English (US) speech recognition. On a non-English Windows add it in
  Settings -> Time & language -> Speech. Without it voice commands are off; the whistle still works.


INSTALL
-------
Copy everything from this zip into the game folder - the folder with RDR2.exe in it, not a subfolder
(a file one folder too deep is simply never loaded):
    DualSenseRDR2.asi
    DualSenseRDR2\  (folder with DualSenseCore.dll, weapons.json, haptics.json, sound_labels.json,
                     speech_voices.txt, translations.json)
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
Bow        R2 gets heavier the further you draw, the wood creaks as it bends, your arm strains if you hold
           too long; letting the string down early feels different from loosing the arrow.
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
Menus      Triggers go slack and the controller goes quiet in the main menu, the pause menu and loading screens.
Dialogue   (experimental, off by default) Every spoken line as the speaker's voice in your hands - see below.
Honor      A warm swell and gold glow when honor rises, a sinking thud when it falls.
Camp life  Bites, sips and puffs, cleaning your gun, brushing / feeding the horse, skinning, stowing pelts,
           multi-stage herb plucking (stem snap + root pull), bathing, crafting, cooking.
Table games Five finger fillet (every stab - and the miss), poker and blackjack chips and cards, dominoes.
Weapon wheel L2/R2 click like a selector switch while you browse.
Lightbar   Muzzle flash, hits, Dead Eye, lantern flicker, wanted police flash, low health, time of day.
           Player LEDs (under the touchpad): rounds left while armed, your health for a few seconds when it
           changes, a police sweep while you're wanted.
Speaker    (off by default: Sounds from the Controller) The game's own sounds for drawing and holstering, the
           lasso, the bow and blades, and clicks for cocking, every round you load, dry fire, the reel, the
           weapon wheel and Dead Eye marks. Off mutes the speaker completely.


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
Original DualSense RDR2 architecture & mod by OldKrab (https://www.nexusmods.com/reddeadredemption2/mods/11444).
Continued as an independent Community Edition by TochaRude (https://github.com/TochaRude) with OldKrab's
blessing (bow, lasso, fists, mounting, trains, weather, carrying, player LED modes, 63-row 8-language settings UI,
touchpad gestures, 93-weapon support, ScriptHookRDR2 V2 stability, and exit controller reset).
Game research: alloc8or's RDR3 native database, femga's rdr3_discoveries, Halen84 and others' decompiled
script references. nlohmann/json (MIT). stb_vorbis by Sean Barrett (public domain). Script Hook RDR2 by
Alexander Blade. Haptics over Bluetooth: the report format worked out by SAxense (https://apps.sdore.me/SAxense).
Source code (MIT): https://github.com/TochaRude/rdr2-dualsense


CHANGES
-------
2.0.0  Community Edition release (superseding v1.3.0-fork.1..v1.3.0-fork.3 & v1.4.0-fork.1-pre.1):
       - Expanded in-game DualSense settings page (63 rows / 7 categories, rebuilt startup_data.rpf)
         including global Triggers.Strength (0-200%) and configurable Touchpad gestures.
       - Automatic multi-language settings UI (EN, DE, FR, ES, IT, PT, PL, and native-reviewed RU) with
         English fallback.
       - Full-travel lever/bolt/pump cycling with mid-stroke detents & dirty slips (HammerSlip), refined
         double-action revolver indexing, and complete 93-weapon coverage.
       - Quick-Draw / Duel R2 tension wall (triggers stay free on Circle melee), lit fuse crackle +
         flicker for dynamite/molotovs, Eagle Eye glow & chime, progressive gun cleaning (gritty ->
         silky), boat rowing & hull wave lap, spooked horse rein tugs, and wagon braking judder.
       - Authentic RDR2 .awc soundbank haptics (FEET_MATERIALS, WATER, MELEE, WEATHER.THUNDER,
         FRONTEND), lasso cadence/dedup fix, and gated ui_confirm on loaded main/pause menus.
       - Clean DualSense hardware reset on game exit (clears lightbar, player LEDs, mute LED, triggers,
         rumble, and USB speaker route and releases the HID handle cleanly).
       - Includes all stability fixes: CRT-free ASI shim + pinned DualSenseCore.dll for ScriptHookRDR2 V2
         save-load reloads, zero exit crashes, and drift-free gyro calibration.
1.4.0  New, from TochaRude: punches, blocks and hits felt in the triggers, a heavier blow for a knockout;
       the bow gets heavier as you draw it, creaks and strains; the lasso stiffens both triggers while you
       hold someone; the fishing cast loads up on R2; a thump when you get on or off a horse or a wagon, and
       when the horse lands a jump; passing trains you can feel; hail, storm gusts and thunder; carrying a
       body makes every step heavier.
       Player LEDs: Auto (rounds while armed, your health when it changes), Rounds, Health or Off - and a
       police sweep while you're wanted, with a heartbeat that speeds up with the wanted level.
       Sounds from the Controller: the game's own sounds for the lasso, the bow and blades; Off now mutes
       the speaker completely.
       Game Sounds and Player LEDs are on the settings page; Rain is now Weather.
       Fixed: Dead Eye marks tick (they never did); no wagon ruts in boats; triggers stay slack in the pause
       menu; the whistle no longer answers Arthur's own whistle; the shot point stops jumping between two
       places.
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
