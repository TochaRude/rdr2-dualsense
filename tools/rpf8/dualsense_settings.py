#!/usr/bin/env python3
"""The mod's settings as shown on the in-game "DualSense" page - the one list everything is built from.

  build_dualsense_page.py   the page (one game stepper per row, sub headers between groups)
  text_inject.cpp (.asi)    the texts (added to the game's loaded string table at run time)
  this script               mod/src/settings_rows.inc (SettingsMenu::entries(); the .asi fills the page's
                            data store from it and writes the page's changes back to the ini) and
                            mod/src/settings_texts.inc (every text of the row and the page; the .asi adds them
                            to the game's loaded string table at run time - any game language)

Usage: dualsense_settings.py      -> writes mod/src/settings_rows.inc and mod/src/settings_texts.inc

Row kinds:
  toggle  ini 0/1          option stepper, options Off / On (the game's own MO_OFF / MO_ON); value = index
  level   ini 0.0 .. max   number stepper in percent (value 0 .. max*100, any whole percent)
  choice  ini one of them  option stepper, options = the given texts (DS_<ID>_<VALUE>); value = index
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
INC = os.path.join(REPO, "mod", "src", "settings_rows.inc")
TEXTS_INC = os.path.join(REPO, "mod", "src", "settings_texts.inc")

# the "DualSense" row in Settings > Controls (make_lml_dualsense.py puts these labels in controls.ymt)
ENTRY_LABELS = {
    "DUALSENSE_SETTINGS": "DualSense",
    "DUALSENSE_SETTINGS_DESC": "Adaptive triggers, haptics, microphone, lightbar and speaker of the DualSense.",
}


def header(text):
    return dict(kind="header", text=text)


def toggle(id, section, key, label, default, desc):
    return dict(kind="toggle", id=id, section=section, key=key, label=label, default=default, desc=desc)


def level(id, section, key, label, default, max, desc):
    return dict(kind="level", id=id, section=section, key=key, label=label, default=default, max=max, desc=desc)


def choice(id, section, key, label, default, choices, desc):
    return dict(kind="choice", id=id, section=section, key=key, label=label, default=default, choices=choices,
                desc=desc)


TOUCHPAD_CHOICES = [
    ("OFF", "Off"),
    ("BACK", "Camera View (Touchpad / Back)"),
    ("CINEMATIC", "Cinematic Camera (Hold Back)"),
    ("MAP", "Open Map (Hold Start)"),
    ("LS", "Calm / Pat Horse / Crouch (L3)"),
    ("EAGLE_EYE", "Eagle Eye (L3 + R3)"),
    ("UP", "Whistle for Horse (D-Pad Up)"),
    ("LONG_WHISTLE", "Long Whistle / Follow (Hold Up)"),
    ("DOWN", "HUD / Info (D-Pad Down)"),
    ("SATCHEL", "Open Satchel (Hold D-Pad Right)"),
    ("JOURNAL", "Open Journal (Hold D-Pad Left)"),
    ("LEFT", "Log / Tasks (D-Pad Left)"),
    ("RIGHT", "D-Pad Right"),
    ("START", "Pause Menu (Start)"),
    ("LB", "Weapon Wheel (L1)"),
    ("RB", "Cover / Horse Brake (R1)"),
    ("RS", "Look Behind / Dead Eye (R3)"),
    ("Y", "Mount / Interact (Triangle)"),
    ("X", "Jump / Reload (Square)"),
    ("B", "Melee / Cancel (Circle)"),
    ("A", "Sprint / Confirm (Cross)"),
]


ROWS = [
    header("Triggers"),
    toggle("triggers_enabled", "Triggers", "Enabled", "Adaptive Triggers", True,
           "Every gun its own pull; fishing line on L2."),
    level("triggers_strength", "Triggers", "Strength", "Trigger Strength", 1.0, 2.0,
          "Scales the overall resistance of the adaptive triggers."),
    level("triggers_kick", "Triggers", "KickStrength", "Recoil on the Trigger", 1.0, 2.0,
          "How hard each shot kicks back against your finger."),
    choice("triggers_aim", "Triggers", "AimResistance", "Aim Resistance", "1",
           [("0", "Off"), ("1", "Light"), ("2", "Full")],
           "Resistance on L2 while you aim. Light: only where Arthur starts aiming. Full: over the whole travel."),
    toggle("triggers_cocking", "Triggers", "ManualCocking", "Work the Hammer with R2", True,
           "Single-action guns need their hammer, lever or bolt worked with R2 between shots. "
           "Turn off if you use an automatic cocking mod."),
    toggle("triggers_hammer_slip", "Triggers", "HammerSlip", "Dirty Cycling Slips", True,
           "Badly worn or dirty manual-action guns (hammers, levers, bolts, pumps) can occasionally slip or hitch before latching. Requires gun condition grit."),
    toggle("triggers_fanning", "Triggers", "Fanning", "Fanning the Hammer", True,
           "Hip-firing a single-action revolver with rapid taps fans the hammer."),
    toggle("triggers_condition", "Triggers", "ConditionGrit", "Gun Condition Feel", True,
           "Clean guns pull smoothly; dirty or worn guns feel stiffer and grittier."),
    toggle("triggers_ammo", "Triggers", "AmmoTypeRecoil", "Ammo Type Recoil", True,
           "Express, high-velocity and explosive ammo kicks harder on the trigger."),
    toggle("triggers_tremor", "Triggers", "Tremor", "Trigger Tremor", True,
           "Shaky trigger pull when Arthur is exhausted, freezing or drunk."),
    toggle("triggers_riding", "Triggers", "RidingPulse", "Riding Gait Pulse", False,
           "Subtle pulse on the triggers matching your horse's gait while riding unarmed."),
    toggle("triggers_autoalign", "Triggers", "AutoAlign", "Trigger Auto-Align", True,
           "Learns where your trigger really breaks in-game and aligns the wall there."),
    header("Haptics"),
    toggle("haptics_enabled", "Haptics", "Enabled", "Haptics", True, "Everything you feel in the grips."),
    level("haptics_strength", "Haptics", "Strength", "Overall Strength", 1.0, 2.0, "Scales every haptic effect."),
    toggle("haptics_gameshots", "Haptics", "GameShots", "Game Sound Bank Haptics", True,
           "Build shots and effects from the game's own sound banks (including sound mods)."),
    toggle("haptics_bluetooth", "Haptics", "Bluetooth", "Bluetooth HD Haptics", True,
           "Play HD haptics over Bluetooth. Turn off to use plain rumble motors on Bluetooth."),
    level("haptics_footsteps", "Haptics", "Footsteps", "Footsteps", 0.35, 1.0, "Arthur's steps on the real ground."),
    level("haptics_landing", "Haptics", "Landing", "Landing Impacts", 0.7, 1.0, "Landing from a jump or a drop."),
    level("haptics_actions", "Haptics", "Actions", "World Interactions", 0.5, 1.0,
          "Drawing or holstering weapons, climbing ledges, opening doors."),
    level("haptics_horse", "Haptics", "Horse", "Hooves", 0.3, 1.0, "Every hoof on the real ground."),
    level("haptics_train", "Haptics", "Train", "Trains, Wagons and Boats", 0.6, 1.0,
          "Rail joints on trains, wagon ruts and braking, and rowing in boats."),
    level("haptics_damage", "Haptics", "Damage", "Taking Hits", 1.0, 2.0, "Hits, felt from the side they came from."),
    level("haptics_gunfire", "Haptics", "Gunfire", "Gunfire Nearby", 0.6, 1.0, "The pressure of other people's shots."),
    level("haptics_heartbeat", "Haptics", "Heartbeat", "Heartbeat", 1.0, 1.0,
          "Heartbeat in Dead Eye and at low health."),
    level("haptics_cold", "Haptics", "Cold", "Cold Shivers", 0.5, 1.0, "Subtle shivers in freezing weather."),
    level("haptics_hitconfirm", "Haptics", "HitConfirm", "Hit Confirm Tick", 0.0, 1.0,
          "A subtle tick when your shot hits a target."),
    level("haptics_camp", "Haptics", "CampLife", "Camp Life", 0.6, 1.0,
          "Eating, drinking, brushing the horse, skinning, bathing..."),
    level("haptics_tables", "Haptics", "TableGames", "Table Games", 0.7, 1.0,
          "Five finger fillet, poker, blackjack, dominoes."),
    level("haptics_rumble", "Haptics", "GameRumble", "Game's Own Rumble", 0.15, 1.0,
          "The game's original vibration, played through the haptics."),
    level("haptics_gameaudio", "Haptics", "GameAudio", "Game Sounds", 0.0, 1.0,
          "Everything you hear, softly in your hands: hooves, explosions, cutscenes. Start low."),
    level("haptics_duck", "Haptics", "Duck", "Background Ducking", 0.3, 1.0,
          "Level of background audio and rumble while a shot plays, keeping recoil crisp."),
    level("haptics_weather", "Haptics", "Weather", "Weather", 0.3, 1.0,
          "Rain and hail on the hat, storm gusts, distant thunder."),
    level("haptics_wheel", "Haptics", "WheelClick", "Weapon Wheel Clicks", 0.6, 1.0,
          "L2 and R2 click like a selector while you browse the weapon wheel."),
    header("Lightbar and Speaker"),
    toggle("lightbar_enabled", "Lightbar", "Enabled", "Lightbar", True,
           "Muzzle flash, hits, Dead Eye, Eagle Eye, lit fuses, honor."),
    level("lightbar_brightness", "Lightbar", "Brightness", "Lightbar Brightness", 1.0, 1.0, ""),
    choice("lightbar_tod", "Lightbar", "Color", "Idle Lightbar Colour", "auto",
           [("auto", "Time of Day"), ("off", "Off (Events Only)"), ("white", "White"), ("blue", "Blue"),
            ("cyan", "Cyan"), ("green", "Green"), ("gold", "Gold"), ("orange", "Orange"),
            ("red", "Red"), ("purple", "Purple")],
           "Idle lightbar colour when no event is active. Pick Time of Day, Off, or a static colour (custom #RRGGBB also supported in DualSenseRDR2.ini)."),
    choice("lightbar_leds", "Lightbar", "PlayerLeds", "Player LEDs", "auto",
           [("auto", "Auto"), ("ammo", "Rounds"), ("health", "Health"), ("off", "Off")],
           "The LEDs under the touchpad. Auto: the rounds left while armed, your health when it changes."),
    toggle("speaker_mechanics", "Speaker", "Mechanics", "Sounds from the Controller", False,
           "Gun, bow, lasso, reload and UI sounds from the controller's speaker."),
    level("speaker_volume", "Speaker", "Volume", "Speaker Volume", 0.35, 1.0, ""),
    toggle("speaker_ambient", "Speaker", "Ambient", "Ambient Speaker Sounds", True,
           "Environmental and subtle ambient sounds on the controller speaker."),
    toggle("speaker_ui", "Speaker", "UI", "Weapon Wheel & UI Sounds", True,
           "Subtle clicks on the controller speaker when browsing menus and the weapon wheel."),
    header("Dialogue Haptics (Experimental)"),
    toggle("haptics_dialogue", "Haptics", "Dialogue", "Dialogue Haptics", False,
           "Feel every spoken line: the speaker's voice in your hands, from their side."),
    level("haptics_speech", "Haptics", "Speech", "Dialogue Strength", 0.6, 1.0, ""),
    choice("haptics_dialogue_in", "Haptics", "DialogueIn", "Dialogue Haptics In", "everywhere",
           [("cutscenes", "Cutscenes"), ("everywhere", "Everywhere")],
           "Only in cutscenes, or every conversation in the game too."),
    choice("haptics_sound_delay", "Haptics", "SoundDelayMs", "TV / Headphone Delay", "auto",
           [("auto", "Auto"), ("0", "None"), ("30", "30 ms"), ("60", "60 ms"), ("90", "90 ms"), ("120", "120 ms"),
            ("160", "160 ms"), ("200", "200 ms"), ("250", "250 ms"), ("300", "300 ms")],
           "How late your TV or headphones play the sound. Auto knows Bluetooth headphones. Raise it if the voice in your hands comes first."),
    header("Microphone (Experimental)"),
    toggle("mic_whistle", "Mic", "WhistleForHorse", "Whistle for Your Horse", False,
           "Whistle into the controller's microphone to call your horse."),
    level("mic_sensitivity", "Mic", "Sensitivity", "Whistle Sensitivity", 1.0, 2.0,
          "Higher reacts to quieter whistles."),
    level("mic_confirm", "Mic", "Confirm", "Recognition Confirm", 1.0, 1.0,
          "Two thumps and a lightbar flash when a whistle or voice command is recognised."),
    toggle("voice_enabled", "Voice", "Enabled", "Voice Commands", False,
           "Say \"chao\" to greet a stranger and \"easy\" to calm your horse."),
    level("voice_confidence", "Voice", "MinConfidence", "Voice Confidence", 0.7, 1.0,
          "Higher means fewer accidental triggers, but requires speaking more clearly."),
    toggle("voice_fallback", "Voice", "SpeechFallback", "Greeting Speech Fallback", True,
           "If Arthur cannot focus a stranger for an honor greeting, still say hello."),
    header("Gyro (Experimental)"),
    choice("gyro_mode", "Gyro", "Mode", "Gyro Aim", "off",
           [("off", "Off"), ("aim", "While Aiming"), ("always", "Always")],
           "Tilt the controller for fine camera corrections on top of the right stick. Still being tuned."),
    level("gyro_sensitivity", "Gyro", "Sensitivity", "Gyro Sensitivity", 1.0, 2.0,
          "Degrees the camera turns per degree the controller tilts."),
    level("gyro_vertical", "Gyro", "VerticalRatio", "Gyro Vertical Ratio", 0.75, 2.0,
          "Vertical gyro sensitivity relative to horizontal."),
    toggle("gyro_invert_x", "Gyro", "InvertX", "Invert Gyro X", False,
           "Invert horizontal gyro camera movement."),
    toggle("gyro_invert_y", "Gyro", "InvertY", "Invert Gyro Y", False,
           "Invert vertical gyro camera movement."),
    choice("gyro_output", "Gyro", "Output", "Gyro Output", "camera",
           [("camera", "Camera"), ("stick", "Right Stick")],
           "Camera turns directly and precisely; Right Stick feeds into stick input."),
    header("Touchpad"),
    choice("touchpad_click", "Touchpad", "Click", "Touchpad Click", "BACK", TOUCHPAD_CHOICES,
           "Action triggered when clicking the touchpad."),
    choice("touchpad_up", "Touchpad", "SwipeUp", "Swipe Up", "OFF", TOUCHPAD_CHOICES,
           "Action triggered when swiping up on the touchpad."),
    choice("touchpad_down", "Touchpad", "SwipeDown", "Swipe Down", "OFF", TOUCHPAD_CHOICES,
           "Action triggered when swiping down on the touchpad."),
    choice("touchpad_left", "Touchpad", "SwipeLeft", "Swipe Left", "OFF", TOUCHPAD_CHOICES,
           "Action triggered when swiping left on the touchpad."),
    choice("touchpad_right", "Touchpad", "SwipeRight", "Swipe Right", "OFF", TOUCHPAD_CHOICES,
           "Action triggered when swiping right on the touchpad."),
    choice("touchpad_twofinger", "Touchpad", "TwoFingerTap", "Two-Finger Tap", "OFF", TOUCHPAD_CHOICES,
           "Action triggered when tapping the touchpad with two fingers."),
]

# Rows that only matter while a toggle is On (the page follows the toggle live, no script needed):
#   SHOW_IF    shown only while it is On - the toggle's own settings, right under it
#   ACTIVE_IF  greyed out while it is Off - rows of another group that the toggle also switches off (a row
#              can follow one toggle's visibility only, so its own sub rows keep following their own toggle)
SHOW_IF = {
    **{r: "triggers_enabled" for r in ("triggers_strength", "triggers_kick", "triggers_aim", "triggers_cocking",
                                       "triggers_hammer_slip", "triggers_fanning", "triggers_condition",
                                       "triggers_ammo", "triggers_tremor", "triggers_riding",
                                       "triggers_autoalign")},
    **{r: "haptics_enabled" for r in ("haptics_strength", "haptics_gameshots", "haptics_bluetooth",
                                      "haptics_footsteps", "haptics_landing", "haptics_actions",
                                      "haptics_horse", "haptics_train", "haptics_damage",
                                      "haptics_gunfire", "haptics_heartbeat", "haptics_cold",
                                      "haptics_hitconfirm", "haptics_camp", "haptics_tables",
                                      "haptics_rumble", "haptics_gameaudio", "haptics_duck",
                                      "haptics_weather", "haptics_wheel")},
    **{r: "lightbar_enabled" for r in ("lightbar_brightness", "lightbar_tod")},
    **{r: "speaker_mechanics" for r in ("speaker_volume", "speaker_ambient", "speaker_ui")},
    **{r: "haptics_dialogue" for r in ("haptics_speech", "haptics_dialogue_in", "haptics_sound_delay")},
    "mic_sensitivity": "mic_whistle",
    **{r: "voice_enabled" for r in ("voice_confidence", "voice_fallback")},
}
ACTIVE_IF = {   # Haptics Off also silences the controller's speaker and the dialogue haptics (core.cpp, dialogue.cpp)
    "triggers_hammer_slip": "triggers_cocking",
    **{r: "haptics_enabled" for r in ("speaker_mechanics", "speaker_volume", "speaker_ambient", "speaker_ui",
                                      "haptics_dialogue", "haptics_speech", "haptics_dialogue_in",
                                      "haptics_sound_delay")},
}


def _check_depends():
    kinds = {r["id"]: r["kind"] for r in ROWS if r["kind"] != "header"}
    for dep in (SHOW_IF, ACTIVE_IF):
        for row, parent in dep.items():
            assert row in kinds and kinds.get(parent) == "toggle", "bad dependency %s -> %s" % (row, parent)


_check_depends()


def label_key(row):
    if row["kind"] == "header":
        return "DS_HDR_" + "".join(c for c in row["text"].upper() if c.isalnum())
    return "DS_" + row["id"].upper()


def desc_key(row):
    return "DS_" + row["id"].upper() + "_DESC"


def choice_key(row, value):
    return "DS_%s_%s" % (row["id"].upper(), value.upper())


def option_keys(row):
    """Text labels of an option stepper's options, index = data store value (levels: none, a number stepper)."""
    if row["kind"] == "toggle":
        return ["MO_OFF", "MO_ON"]
    if row["kind"] == "level":
        return []
    return [choice_key(row, v) for v, _ in row["choices"]]


# the page without values (title screen: no mod script runs there yet)
NOT_READY_KEY = "DS_PAGE_NOT_READY"
NOT_READY_TEXT = "Load your story to change the DualSense settings."


def labels():
    """Every text of the row and the page: {label key: text}."""
    out = dict(ENTRY_LABELS)
    out[NOT_READY_KEY] = NOT_READY_TEXT
    for r in ROWS:
        if r["kind"] == "header":
            out[label_key(r)] = r["text"]
            continue
        out[label_key(r)] = r["label"]
        if r["desc"]:
            out[desc_key(r)] = r["desc"]
        if r["kind"] == "choice":
            for v, text in r["choices"]:
                out[choice_key(r, v)] = text
    return out


def c_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def c_float(v):
    s = "%g" % v
    return s + ("f" if "." in s else ".0f")


def inc_text():
    lines = ["// Generated by tools/rpf8/dualsense_settings.py - edit the list there, then run it and",
             "// build_dualsense_page.py (the in-game page and its texts come from the same list).",
             "// clang-format off"]
    for r in ROWS:
        k = r["kind"]
        if k == "header":
            lines.append("header(%s, %s)," % (c_str(label_key(r)), c_str(r["text"])))
            continue
        common = "%s, %s, %s, %s, %s" % (c_str(r["id"]), c_str(r["section"]), c_str(r["key"]),
                                        c_str(label_key(r)), c_str(r["label"]))
        if k == "toggle":
            lines.append("toggle(%s, %s, %s)," % (common, "true" if r["default"] else "false", c_str(r["desc"])))
        elif k == "level":
            lines.append("level(%s, %s, %s, %s)," % (common, c_float(r["default"]), c_float(r["max"]), c_str(r["desc"])))
        else:
            ch = ", ".join("{%s, %s, %s}" % (c_str(v), c_str(t), c_str(choice_key(r, v))) for v, t in r["choices"])
            lines.append("choice(%s, %s, {%s}, %s)," % (common, c_str(r["default"]), ch, c_str(r["desc"])))
    lines.append("// clang-format on")
    return "\n".join(lines) + "\n"


def texts_inc_text():
    lines = ["// Generated by tools/rpf8/dualsense_settings.py - the texts of the Settings row and the DualSense page",
             "// (label key, English fallback text); text_inject.cpp overlays translations from translations.json.",
             "// clang-format off"]
    lines += ["{%s, %s}," % (c_str(k), c_str(v)) for k, v in labels().items()]
    lines.append("// clang-format on")
    return "\n".join(lines) + "\n"


def main():
    ids = [r["id"] for r in ROWS if r["kind"] != "header"]
    assert len(ids) == len(set(ids)), "duplicate row id"
    assert all(i.replace("_", "").isalnum() and i == i.lower() for i in ids)
    with open(INC, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(inc_text())
    with open(TEXTS_INC, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(texts_inc_text())
    print("%s: %d rows, %d headers; %s: %d texts" % (INC, len(ids), len(ROWS) - len(ids), TEXTS_INC, len(labels())))


if __name__ == "__main__":
    main()
