#include "config.h"

#include "log.h"

#include <charconv>

#include <windows.h>

#include <cctype>
#include <cmath>
#include <filesystem>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <mutex>
#include <vector>

namespace dsr2 {

Config& config() {
    static Config* c = new Config();
    return *c;
}

static std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

static std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}

const char* Config::default_text() {
    return R"INI(; ============================================================================
;  DualSense for RDR2 - settings. Edit and restart the game (or press F9 in game to reload).
; ============================================================================

[General]
Enabled=1
; 1 = write detailed events to DualSenseRDR2.log
Debug=0
; The game only sees the DualSense (hides DSX/ViGEm virtual pads that would double the input)
HideOtherPads=1
; Key (virtual-key code) that reloads this file and the data folder in game. 120 = F9 (or hold PS ~1 s)
ReloadKey=120
; Which defaults this file has. A newer version of the mod moves settings still at an older default to the new
; one (settings you changed stay). Leave it alone.
DefaultsVersion=3
; Ground is identified from the game's material automatically. This key shows what is under you;
; pressing it again while shown changes it (ground, grass, mud, stone, gravel, wood, snow, sand, water).
; Saved in DualSenseRDR2\materials.json. 121 = F10
TeachSurfaceKey=121
; In-game DualSense Settings language: auto (matches game language, English fallback), en, de, fr, es, it, pt, pl, ru
Language=auto

[Triggers]
Enabled=1
; Global adaptive trigger strength (0 - 2)
Strength=1.0
; RDR2 cycles single-action hammers, levers, bolts and pumps with an R2 tap between shots.
; Set 0 if you use the "Automatic Cocking" mod.
ManualCocking=1
; Hip-firing a single-action revolver with rapid taps fans the hammer
Fanning=1
; Learn where your trigger really breaks and fire exactly there
AutoAlign=1
; Raw units past the break zone before the shot (when not yet aligned)
FireMargin=4
; Trigger recoil strength (0 - 2)
KickStrength=1.0
; Resistance on L2 while aiming: 0 = off, 1 = light (only where the game starts aiming, the rest of
; the travel is free), 2 = full (the gun's weight over the whole travel)
AimResistance=1
; Clean guns work like clockwork, dirty / worn ones drag like mud (the game's real gun condition)
ConditionGrit=1
; Badly worn / dirty manual-action guns (hammers, levers, bolts, pumps) can occasionally slip/hitch before latching (0 = off, 1 = on)
HammerSlip=1
; Express / explosive ammo kicks harder
AmmoTypeRecoil=1
; Shaky trigger when exhausted, freezing or drunk
Tremor=1
; Triggers pulse with the horse's gait while riding unarmed (0 = off, 1 = on)
RidingPulse=0

[Haptics]
Enabled=1
; Master strength (0 - 2)
Strength=1.0
; Build every gun's shot from its own recordings in the game's sound banks (lml sound mods)
GameShots=1
; The game's own rumble, converted for the voice-coil actuators (0 = off)
GameRumble=0.15
; Haptics over Bluetooth too (0 = only the plain rumble motors over Bluetooth)
Bluetooth=1
; Everything you hear, turned into haptics (0 = off). Off by default: it also turns music and
; other sounds not made by your character into vibration. Try 0.3 if you want it anyway.
GameAudio=0
; Part of the name of the output to listen to. Empty = the default output (where the game plays)
GameAudioDevice=
; Level of game audio / rumble while a weapon effect plays (so shots stay crisp)
Duck=0.3
; Taking damage, felt from the direction it came from
Damage=1.0
; Pressure wave of nearby gunfire, from its direction
Gunfire=0.6
; Hoofbeats by gait and ground, horse's heartbeat when it is exhausted
Horse=0.3
; Rail joints on trains, ruts on wagons
Train=0.6
; Heartbeat in Dead Eye and at low health
Heartbeat=1.0
; Shivers in freezing weather
Cold=0.5
; A tiny tick when your shot hits someone
HitConfirm=0
; Footsteps on foot, timed by Arthur's real feet, felt per surface (left foot left, right foot right)
Footsteps=0.35
; Landing from a jump or a drop
Landing=0.7
; Grabbing a ledge, opening a door, drawing / holstering a gun
Actions=0.5
; Five finger fillet (knife taps, the miss), poker / blackjack chips and cards, dominoes tiles
TableGames=0.7
; The little click of L2/R2 while browsing the weapon wheel (0 = off)
WheelClick=0.6
; Camp life: bites, sips and puffs, cleaning your gun, brushing / feeding the horse, skinning, stowing pelts,
; picking herbs, bathing, crafting, cooking, carrying bodies
CampLife=0.6
; Rain / hail drops, storm wind gusts, distant thunder (0 = off)
Weather=0.3
; EXPERIMENTAL - dialogue haptics: every spoken line felt as the speaker's voice (its rhythm and intonation, from
; their side). Dialogue = on / off, Speech = its strength, DialogueIn = everywhere / cutscenes
Dialogue=0
Speech=0.6
DialogueIn=everywhere
; How late your TV / headphones play the sound, in ms, so the dialogue haptics land with it:
; auto = by the output (Bluetooth headphones 200, TV / wired 60), or a number
SoundDelayMs=auto

[Speaker]
; Sounds from the controller speaker: gun/bow/lasso mechanics, reloads, and crisp mechanical clicks (off by default)
Mechanics=0
; Sub-categories when Mechanics=1 (set 0 to mute a category on the speaker)
Ambient=1
UI=1
Volume=0.35

[Lightbar]
Enabled=1
Brightness=1.0
; Player LEDs below the touchpad: auto (ammo when armed, health changes when unarmed), health, ammo, or off
PlayerLeds=auto
; Ambient colour follows the time of day when nothing else is shown
TimeOfDay=1

[Gyro]
; EXPERIMENTAL - still being tuned (can feel floaty). Off by default.
; Gyro aim: tilt the controller for fine corrections while aiming - on top of the right stick, not instead of it.
; aim = only while free-aiming (never during the game's lock-on), always = always, off = off
Mode=off
; Degrees the camera turns per degree the controller turns
Sensitivity=1.0
; Vertical relative to horizontal
VerticalRatio=0.75
InvertX=0
InvertY=0
; Movements slower than this (deg/s) are smoothed (hand tremor); faster ones pass straight through
SmoothThreshold=6
; camera = turns the camera directly (instant, precise; pauses while you use the right stick)
; stick = adds to the right stick instead (then StickRate / StickDeadzone matter)
Output=camera
; Stick output: the camera speed at full stick (deg/s) and the game's stick dead zone
StickRate=90
StickDeadzone=0.2

[Mic]
; Whistle into the controller to call your horse. The mute button toggles listening.
WhistleForHorse=0
; Higher = reacts to quieter whistles
Sensitivity=1.0
; Two firm thumps + a blue lightbar flash when a whistle or your phrase is recognised (0 = off)
Confirm=1.0
; Seconds after a recognised whistle before the next one counts (at least 3: Arthur whistles back)
Cooldown=3

[Voice]
; Say the phrase into the controller: the camera turns to the closest person (within Range metres) and
; Arthur greets them for real - exactly like focusing them and choosing Greet, so it counts for honor.
; Only the game's own focus + Greet actions are used: it can never aim, draw or attack. Two thumps = done.
; Gun must be holstered. If the game won't focus them, Arthur just says hello (one soft thump, no honor).
; English speech recognizer (built into Windows). The mute button pauses listening.
Enabled=0
; Several words allowed, comma separated (chao: the recognizer knows it as "ciao" / "chow")
Phrase=ciao,chow
Range=6
; 0-1: higher = fewer accidental greetings, but you may need to say it more clearly
MinConfidence=0.7
; If the real greeting fails, Arthur still says a greeting line (no honor)
SpeechFallback=1
; Calm your horse (riding it or standing next to it) - the game's own Calm
CalmPhrase=easy,easy boy,easy girl,steady
; Experimental, off by default (they can trigger by accident while you talk): stop / spur the horse
StopPhrase=
SpeedPhrase=
SpurTaps=2

[Touchpad]
; Button sent to the game: A B X Y LB RB LT RT BACK START LS RS UP DOWN LEFT RIGHT, OFF or empty = nothing
Click=BACK
SwipeUp=
SwipeDown=
SwipeLeft=
SwipeRight=
TwoFingerTap=
)INI";
}

bool Config::load(const std::wstring& path) {
    std::map<std::string, std::string> values;
    std::string text;
    {
        std::ifstream in{std::filesystem::path(path)};
        if (!in) {
            std::ofstream out{std::filesystem::path(path)};
            out << default_text();
            out.close();
            in.open(std::filesystem::path(path));
        }
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            text = ss.str();
        } else {
            text = default_text();
        }
    }   // close file handle before migrate() / MoveFileExW() runs
    std::istringstream lines(text);
    std::string line, section;
    bool first_line = true;
    while (std::getline(lines, line)) {
        if (first_line) {
            first_line = false;
            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
                line.erase(0, 3);
        }
        line = trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = lower(trim(line.substr(0, eq)));
        std::string val = trim(line.substr(eq + 1));
        size_t c = std::min(val.find(" ;"), val.find("\t;"));
        if (c != std::string::npos) val = trim(val.substr(0, c));
        values[section + "." + key] = val;
    }
    {
        std::unique_lock<std::shared_mutex> l(mu_);
        path_ = path;
        values_.swap(values);
    }
    migrate();
    return true;
}

namespace {
// Defaults that changed: a player's ini written by an older version still holds the old value. Moved to the new one
// when it is still exactly the old default (the player never changed it); anything else is the player's choice.
constexpr int kDefaultsVersion = 3;
struct DefaultChange {
    int version;
    const char *section, *key, *old_value, *new_value;
};
constexpr DefaultChange kChanges[] = {
    {2, "Speaker", "Mechanics", "1", "0"},           // sounds from the controller: off (out of sync, players asked)
    {2, "Haptics", "Weather", "0", "0.3"},           // rain drops on the hat: on, subtle
    {2, "Haptics", "GameRumble", "0.35", "0.15"},    // the game's own rumble: quieter (pulses on herbs and prompts)
    {3, "Mic", "Cooldown", "1.2", "3"},              // Arthur's own whistle from the speakers counted as a new one
};

bool same_value(const std::string& a, const std::string& b) {
    char* ea = nullptr;
    char* eb = nullptr;
    double x = std::strtod(a.c_str(), &ea), y = std::strtod(b.c_str(), &eb);
    if (ea && *ea == 0 && eb && *eb == 0 && !a.empty() && !b.empty()) return std::fabs(x - y) < 1e-4;
    return lower(a) == lower(b);
}
}  // namespace

void Config::migrate() {
    if (migrating_) return;                               // set() reloads the file
    int have = integer("General", "DefaultsVersion", 1);
    if (have >= kDefaultsVersion) return;
    migrating_ = true;
    for (const auto& c : kChanges) {
        if (c.version <= have) continue;
        std::string cur = str(c.section, c.key, "");
        if (cur.empty() || !same_value(cur, c.old_value)) continue;     // missing (the code default) or changed
        set(c.section, c.key, c.new_value);
        LOGI("settings: %s.%s %s -> %s (the new default; you had not changed it)", c.section, c.key, c.old_value,
             c.new_value);
    }
    // 3: the player LEDs got modes (PlayerLeds); before, AmmoLeds=0 switched them off
    if (have < 3 && str("Lightbar", "PlayerLeds", "").empty() && !flag("Lightbar", "AmmoLeds", true)) {
        set("Lightbar", "PlayerLeds", "off");
        LOGI("settings: Lightbar.PlayerLeds = off (you had AmmoLeds=0)");
    }
    set("General", "DefaultsVersion", std::to_string(kDefaultsVersion));
    migrating_ = false;
}

bool Config::set(const std::string& section, const std::string& key, const std::string& value) {
    return set_many({{section, key, value}});
}

bool Config::set_many(const std::vector<std::tuple<std::string, std::string, std::string>>& updates) {
    if (updates.empty()) return true;
    std::wstring path_copy;
    {
        std::lock_guard<std::mutex> wl(write_mu_);
        {
            std::shared_lock<std::shared_mutex> l(mu_);
            path_copy = path_;
        }
        if (path_copy.empty()) return false;
        std::filesystem::path fpath(path_copy);
        std::vector<std::string> lines;
        {
            std::ifstream in{fpath};
            if (!in) {
                std::error_code ec;
                if (std::filesystem::exists(fpath, ec)) return false;   // never wipe an existing ini on transient open failure
            } else {
                bool first = true;
                for (std::string l; std::getline(in, l);) {
                    if (first) {
                        first = false;
                        if (l.size() >= 3 && (unsigned char)l[0] == 0xEF && (unsigned char)l[1] == 0xBB && (unsigned char)l[2] == 0xBF)
                            l.erase(0, 3);
                    }
                    if (!l.empty() && l.back() == '\r') l.pop_back();
                    lines.push_back(std::move(l));
                }
            }
        }
        for (const auto& [section, key, value] : updates) {
            std::string want_s = lower(section), want_k = lower(key), cur;
            int section_end = -1, section_line = -1;
            bool done = false;
            for (size_t i = 0; i < lines.size() && !done; ++i) {
                std::string t = trim(lines[i]);
                if (!t.empty() && t.front() == '[' && t.back() == ']') {
                    if (cur == want_s) section_end = int(i);
                    cur = lower(trim(t.substr(1, t.size() - 2)));
                    if (cur == want_s) section_line = int(i);
                    continue;
                }
                if (cur != want_s || t.empty() || t[0] == ';' || t[0] == '#') continue;
                size_t eq = t.find('=');
                if (eq == std::string::npos || lower(trim(t.substr(0, eq))) != want_k) continue;
                size_t c = std::min(lines[i].find(" ;"), lines[i].find("\t;"));
                std::string tail = c != std::string::npos ? lines[i].substr(c) : "";
                lines[i] = trim(t.substr(0, eq)) + "=" + value + tail;
                done = true;
            }
            if (!done) {
                if (section_line < 0) {
                    if (!lines.empty() && !trim(lines.back()).empty()) lines.push_back("");
                    lines.push_back("[" + section + "]");
                    lines.push_back(key + "=" + value);
                } else {
                    int at = section_end >= 0 ? section_end : int(lines.size());
                    while (at > section_line + 1 && trim(lines[at - 1]).empty()) --at;
                    lines.insert(lines.begin() + at, key + "=" + value);
                }
            }
        }
        std::wstring tmp_path = path_copy + L".tmp";
        {
            std::ofstream out{std::filesystem::path(tmp_path), std::ios::binary | std::ios::trunc};
            for (auto& l : lines) out << l << "\r\n";
            out.flush();
            if (!out) return false;
        }
        if (!MoveFileExW(tmp_path.c_str(), path_copy.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            DeleteFileW(tmp_path.c_str());
            return false;
        }
    }   // release write_mu_ before load() / migrate() so migration can never deadlock
    return load(path_copy);
}

std::string Config::str(const std::string& s, const std::string& k, const std::string& def) const {
    std::shared_lock<std::shared_mutex> l(mu_);
    auto it = values_.find(lower(s) + "." + lower(k));
    return it == values_.end() ? def : it->second;
}

float Config::num(const std::string& s, const std::string& k, float def) const {
    std::string v = str(s, k, "");
    if (v.empty()) return def;
    // locale-independent: the game switches the C locale (e.g. to a decimal comma), and std::stof would read
    // "0.75" as 0 there. from_chars always uses '.'; a ',' typed by hand is accepted too.
    for (auto& ch : v) if (ch == ',') ch = '.';
    float out = def;
    const char* b = v.data();
    while (*b == ' ' || *b == '+') ++b;
    auto r = std::from_chars(b, v.data() + v.size(), out);
    return (r.ec == std::errc() && std::isfinite(out)) ? out : def;
}

int Config::integer(const std::string& s, const std::string& k, int def) const {
    return int(std::lround(num(s, k, float(def))));
}

bool Config::flag(const std::string& s, const std::string& k, bool def) const {
    std::string v = lower(str(s, k, ""));
    if (v.empty()) return def;
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

}  // namespace dsr2
