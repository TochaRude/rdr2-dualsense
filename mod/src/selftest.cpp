// Offline checks of the C++ port against the behaviour verified in the Python lab tests.
#include "awc.h"
#include "catalog.h"
#include "core.h"
#include "dialogue.h"
#include "config.h"
#include "dsp.h"
#include "engine.h"
#include "features.h"
#include "gameaudio.h"
#include "haptics.h"
#include "input.h"
#include "surfaces.h"
#include "settings_menu.h"
#include "text_inject.h"
#include "voice.h"
#include "world.h"
#include "log.h"

#include <clocale>
#include <complex>
#include <fstream>
#include <cstdio>
#include <set>

namespace dsr2 {

namespace {

int g_fail = 0, g_pass = 0;
#define CHECK(cond, what)                                                       \
    do {                                                                        \
        if (cond) ++g_pass;                                                     \
        else { ++g_fail; printf("  FAIL %s  (%s:%d)\n", what, __FILE__, __LINE__); } \
    } while (0)

struct Rig {
    WeaponEngine eng;
    double t = 1.0;
    std::vector<EngineFrame> frames;
    std::vector<int> raws;
    GameHints hints;
    bool game_fires = true;
    int last_rt_ = 0, rt_prev_ = 0;
    Rig(const nlohmann::json& p, Settings s = Settings{}) : eng(p, Calibration{}, s, {}) {}
    Rig& feed(const std::vector<int>& rs, uint32_t buttons = 0, int l2 = 0) {
        for (int r : rs) {
            t += 0.004;
            InputState in;
            in.t = t;
            in.r2 = r;
            in.l2 = l2;
            in.buttons = buttons;
            in.fresh = true;
            raws.push_back(r);
            // the game: it fires the frame after it sees R2 go down (when it is in game and wants to fire)
            if (hints.valid && game_fires) hints.shooting = rt_prev_ == 0 && last_rt_ == 255;
            rt_prev_ = last_rt_;
            frames.push_back(eng.update(in, hints));
            last_rt_ = std::max(0, frames.back().game_rt);
        }
        return *this;
    }
    Rig& hold(int raw, double s, int l2 = 0) { return feed(std::vector<int>(size_t(s / 0.004), raw), 0, l2); }
    Rig& pull(int to = 255, int n = 40, int l2 = 0) {
        std::vector<int> v;
        for (int i = 0; i <= n; ++i) v.push_back(to * i / n);
        return feed(v, 0, l2);
    }
    Rig& release(int n = 10, int l2 = 0) { return feed(std::vector<int>(size_t(n), 0), 0, l2); }
    int count(const std::string& cue) const {
        int c = 0;
        for (auto& f : frames) for (auto& q : f.cues) c += q.name == cue;
        return c;
    }
    int shots_rt() const {     // rising edges of the RT the game would see
        int c = 0, prev = 0;
        for (auto& f : frames) { int v = std::max(0, f.game_rt); c += prev == 0 && v == 255; prev = v; }
        return c;
    }
};

}  // namespace

int run_selftests(const fs::path& data) {
    g_fail = 0;
    g_pass = 0;
    // dialogue haptics: the spoken line's recording id from the game's speech data (a real greeting: context kept
    // before joaat's last mixing step, variation 1 -> the stream the game read from disk)
    CHECK(awc::line_stream_id(0xB87C92A7, 1) == 0x0C96A2A6, "speech line id: partial context hash + variation");
    CHECK(awc::joaat_finish(0) == 0 && (joaat("greet_male") == awc::joaat_finish([] {
              uint32_t h = 0;
              for (char c : std::string("greet_male")) { h += uint8_t(c); h += h << 10; h ^= h >> 6; }
              return h;
          }())), "joaat = partial + finish");
    {
        std::vector<float> voice(48000, 0.0f);
        for (size_t i = 0; i < voice.size(); ++i)          // a 'syllable' 0.2-0.4 s
            if (i > 9600 && i < 19200)                     // a voiced 130 Hz 'vowel' with its harmonics
                for (int h = 1; h <= 20; ++h) voice[i] += 0.3f / float(h) * std::sin(2 * kPi * 130.0f * float(h) * float(i) / 48000);
        auto sp = speech_haptic(voice);
        float in = 0, out = 0;
        for (size_t i = 12000; i < 18000; ++i) in = std::max(in, std::fabs(sp[i]));
        for (size_t i = 30000; i < 48000; ++i) out = std::max(out, std::fabs(sp[i]));
        CHECK(in > 0.3f && out < 0.02f, "speech vibration follows the syllable");
    }
    Catalog cat;
    cat.load(data / "weapons.json");
    std::string name;
    nlohmann::json catt = cat.resolve(joaat("WEAPON_REVOLVER_CATTLEMAN"), 0, name);
    CHECK(!catt.is_null() && name == "WEAPON_REVOLVER_CATTLEMAN", "resolve by game hash");
    CHECK(cat.resolve_name("WEAPON_REVOLVER_LEMAT").value("clip", 0) == 9, "tune overrides class (LeMat clip 9)");
    nlohmann::json fb = cat.resolve(0x12345678, joaat("GROUP_REPEATER"), name);
    CHECK(!fb.is_null() && name.find("CARBINE") != std::string::npos, "unknown weapon falls back to its group");
    {   // Story/Online variants & unique weapons resolve with matching class, bank, and melee feel
        auto mauser = cat.resolve(joaat("WEAPON_PISTOL_MAUSER_DRUNK"), 0, name);
        CHECK(name == "WEAPON_PISTOL_MAUSER_DRUNK" && mauser.value("clip", 0) == 10 &&
                  mauser.value("bank", "") == "PTL_MAUSER_FINAL",
              "Midnight's Pistol resolves as 10-round Mauser C96");
        auto granger = cat.resolve(joaat("WEAPON_REVOLVER_CATTLEMAN_PIG"), 0, name);
        auto john_rev = cat.resolve(joaat("WEAPON_REVOLVER_CATTLEMAN_JOHN"), 0, name);
        auto lowry = cat.resolve(joaat("WEAPON_REVOLVER_NAVY_CROSSOVER"), 0, name);
        auto gambler = cat.resolve(joaat("WEAPON_REVOLVER_DOUBLEACTION_GAMBLER"), 0, name);
        CHECK(granger.value("action", nlohmann::json::object()).value("type", "") == "hammer" &&
                  john_rev.value("action", nlohmann::json::object()).value("type", "") == "hammer" &&
                  lowry.value("bank", "") == "REV_NAVY_FINAL" &&
                  gambler.value("label", "") == "High Roller Revolver",
              "unique Cattleman, Lowry's Navy, and High Roller Double-Action resolve cleanly");
        auto viking = cat.resolve(joaat("WEAPON_MELEE_HATCHET_VIKING"), 0, name);
        auto sword = cat.resolve(joaat("WEAPON_MELEE_BROKEN_SWORD"), 0, name);
        auto hammer = cat.resolve(joaat("WEAPON_MELEE_HAMMER"), 0, name);
        auto bolas = cat.resolve(joaat("WEAPON_THROWN_BOLAS_HAWKMOTH"), 0, name);
        CHECK(viking["melee"].value("impact", "") == "impact_chop" &&
                  sword["melee"].value("impact", "") == "impact_chop" &&
                  hammer["melee"].value("impact", "") == "impact_blunt" &&
                  bolas["recoil"].value("haptic", "") == "bolas",
              "story/RDO melee axes, sword, hammer, and bolas resolve with dedicated recipes");
    }

    {   // one shot at the break, none while held, RT jumps 0 -> 255 exactly there
        Settings s; s.manual_action = false;
        Rig r(catt, s);
        r.pull().hold(255, 1.0);
        CHECK(r.shots_rt() == 1, "single shot, no refire while held");
        size_t first = 0;
        while (first < r.frames.size() && r.frames[first].game_rt != 255) ++first;
        CHECK(first > 0 && first < r.frames.size() && r.raws[first] >= r.eng.break_raw() && r.raws[first - 1] < r.eng.break_raw(),
              "shot fires on the first sample past the break");
    }
    {   // single action: the next tap cocks the hammer (aiming, so no fanning)
        Settings s; s.fanning = false;
        Rig r(catt, s);
        r.pull(255, 40, 200).release(125, 200).pull(255, 40, 200);
        CHECK(r.count("hammer_cock") == 1, "second tap cocks the hammer");
        r.release(80, 200).pull(255, 40, 200);
        CHECK(r.count(r.eng.id() + ":shot") + r.count("shot_revolver") == 2, "third tap fires");
    }
    {   // single-action hammer spring: continuous resistance to bottom-out, mid-stroke notch, condition scaling, optional slip
        Rig r(catt);
        r.hints.valid = true;
        r.hints.hip_fire = false;
        r.hints.grit = 0.0f;
        r.pull(255, 20, 200);
        r.hints.ready = false;
        r.release(30, 200);
        CHECK(r.frames.back().state == "action", "SA revolver enters hammer action after release");
        auto z_clean = fx::zones_of(r.frames.back().r2);
        CHECK(z_clean[1] >= 2 && z_clean[5] >= z_clean[1] && z_clean[9] >= z_clean[5] && z_clean[8] > 0,
              "hammer action has continuous spring resistance through bottom-out without a break cliff");
        r.pull(200, 20, 200);
        CHECK(r.frames.back().state == "action" && r.count("hammer_cock") == 0 && r.count("cylinder_index") >= 1,
              "partway pull triggers mid-stroke notch without cocking early");
        r.pull(255, 10, 200);
        CHECK(r.frames.back().state == "cycling" && r.count("hammer_cock") == 1,
              "full press near bottom-out cocks the hammer");

        Rig d(catt);
        d.hints.valid = true;
        d.hints.hip_fire = false;
        d.hints.grit = 0.9f;
        d.pull(255, 20, 200);
        d.hints.ready = false;
        d.release(30, 200);
        auto z_dirty = fx::zones_of(d.frames.back().r2);
        CHECK(z_dirty[1] > z_clean[1] && z_dirty[5] > z_clean[5],
              "worn/dirty gun makes the hammer spring strictly stiffer");

        auto test_slips = [&](bool allow_slip) {
            Settings hs;
            hs.hammer_slip = allow_slip;
            Rig q(catt, hs);
            q.hints.valid = true;
            q.hints.hip_fire = false;
            q.hints.grit = 1.0f;
            int slips = 0;
            for (int cycle = 0; cycle < 40; ++cycle) {
                q.hints.ready = true;
                q.hints.clip = 6;
                q.release(45, 200).pull(255, 10, 200);
                q.hints.ready = false;
                q.release(45, 200);
                int before = q.count("hammer_cock");
                q.pull(255, 10, 200);
                if (q.frames.back().state == "action") {
                    ++slips;
                    CHECK(q.count("hammer_cock") == before, "hammer slip did not cock early");
                    q.release(30, 200).pull(255, 10, 200);
                    CHECK(q.count("hammer_cock") == before + 1 && q.frames.back().state == "cycling",
                          "retry after hammer slip cocks reliably");
                }
                q.hints.ready = true;
                q.hold(255, 0.1, 200);
            }
            return slips;
        };
        CHECK(test_slips(true) > 0, "dirty gun with HammerSlip=1 can slip occasionally");
        CHECK(test_slips(false) == 0, "dirty gun with HammerSlip=0 never slips");
    }
    {   // lever action: full-travel curve, mid-stroke detents, Evans magazine hitch, dirty drag + optional slip
        auto check_action_slips = [&](const std::string& wpn, const std::string& open_cue, bool allow_slip) {
            Settings hs;
            hs.hammer_slip = allow_slip;
            Rig q(cat.resolve_name(wpn), hs);
            q.hints.valid = true;
            q.hints.hip_fire = false;
            q.hints.grit = 1.0f;
            int slips = 0;
            for (int cycle = 0; cycle < 60; ++cycle) {
                q.hints.ready = true;
                q.hints.clip = 6;
                q.release(75, 200).pull(255, 10, 200);
                q.hints.ready = false;
                q.release(75, 200);
                q.pull(255, 10, 200);
                if (q.frames.back().state == "action") {
                    ++slips;
                    CHECK(q.frames.back().game_rt == 0, (wpn + " slip does not release RT to the game").c_str());
                    int at_slip = q.count(open_cue);
                    q.release(30, 200).pull(255, 10, 200);
                    CHECK(q.count(open_cue) > at_slip && q.frames.back().state == "cycling" &&
                              q.frames.back().game_rt == 255,
                          (wpn + " retry after slip cycles cleanly").c_str());
                }
                q.hints.ready = true;
                q.hold(255, 0.05, 200);
            }
            return slips;
        };

        Rig r(cat.resolve_name("WEAPON_REPEATER_CARBINE"));
        r.hints.valid = true; r.hints.hip_fire = false; r.hints.grit = 0.0f;
        r.pull(255, 20, 200);
        r.hints.ready = false;
        r.release(65, 200);
        auto az = fx::zones_of(r.frames.back().r2);
        CHECK(r.frames.back().state == "action" && az[1] >= 2 && az[9] >= 7 && az[9] > az[3],
              "clean lever action preserves progressive full-travel resistance");
        r.pull(190, 20, 200);
        CHECK(r.frames.back().state == "action" && r.count("lever_open") == 0 && r.count("cylinder_index") >= 1,
              "lever does not complete at zone 7 and emits mid-stroke detent");
        r.pull(255, 10, 200);
        CHECK(r.frames.back().state == "cycling" && r.count("lever_open") == 1,
              "lever cycles at full stroke (zone 9)");

        Rig ev(cat.resolve_name("WEAPON_REPEATER_EVANS"));
        ev.hints.valid = true; ev.hints.hip_fire = false;
        ev.pull(255, 20, 200);
        ev.hints.ready = false;
        ev.release(65, 200);
        auto ez = fx::zones_of(ev.frames.back().r2);
        CHECK(ez[5] > ez[6] && ez[9] >= 7, "Evans Repeater keeps helical-magazine hitch through zone 9");
        ev.pull(255, 20, 200);
        CHECK(ev.count("magazine_index") >= 1 && ev.count("lever_open") == 1,
              "Evans emits magazine_index detent and cycles");

        Rig d(cat.resolve_name("WEAPON_REPEATER_CARBINE"));
        d.hints.valid = true; d.hints.hip_fire = false; d.hints.grit = 0.9f;
        d.pull(255, 20, 200); d.hints.ready = false; d.release(65, 200);
        auto dz = fx::zones_of(d.frames.back().r2);
        CHECK(dz[4] > az[4], "dirty lever is stiffer than clean lever");
        CHECK(check_action_slips("WEAPON_REPEATER_CARBINE", "lever_open", true) > 0,
              "dirty lever with HammerSlip=1 can hitch");
        CHECK(check_action_slips("WEAPON_REPEATER_CARBINE", "lever_open", false) == 0,
              "dirty lever with HammerSlip=0 never hitches");

        // bolt action: two-phase unlock/glide/lock profile, detent, dirty drag + slip
        Rig b(cat.resolve_name("WEAPON_RIFLE_BOLTACTION"));
        b.hints.valid = true; b.hints.hip_fire = false; b.hints.grit = 0.0f;
        b.pull(255, 20, 200); b.hints.ready = false; b.release(65, 200);
        auto bz = fx::zones_of(b.frames.back().r2);
        CHECK(bz[1] >= 6 && bz[3] < bz[1] && bz[9] >= 7,
              "bolt action preserves two-phase unlock lift, lighter glide, and lock-down");
        b.pull(190, 20, 200);
        CHECK(b.frames.back().state == "action" && b.count("bolt_open") >= 1 && b.count("bolt_close") == 0,
              "bolt triggers mid-stroke bolt_open detent without locking before zone 9");
        b.pull(255, 10, 200);
        CHECK(b.frames.back().state == "cycling" && b.count("bolt_close") == 1,
              "bolt completes lock-down at full travel");

        Rig bd(cat.resolve_name("WEAPON_RIFLE_BOLTACTION"));
        bd.hints.valid = true; bd.hints.hip_fire = false; bd.hints.grit = 0.9f;
        bd.pull(255, 20, 200); bd.hints.ready = false; bd.release(65, 200);
        auto bdz = fx::zones_of(bd.frames.back().r2);
        CHECK(bdz[3] > bz[3] && bdz[1] >= bdz[3], "dirty bolt stiffens while keeping unlock contrast");
        CHECK(check_action_slips("WEAPON_RIFLE_BOLTACTION", "bolt_close", true) > 0 &&
                  check_action_slips("WEAPON_RIFLE_BOLTACTION", "bolt_close", false) == 0,
              "dirty bolt respects HammerSlip setting");

        // pump action: Pump-Action Shotgun vs Varmint Rifle, reversal detent, dirty drag + slip
        Rig p(cat.resolve_name("WEAPON_SHOTGUN_PUMP"));
        p.hints.valid = true; p.hints.hip_fire = false;
        p.pull(255, 20, 200); p.hints.ready = false; p.release(75, 200);
        auto pz = fx::zones_of(p.frames.back().r2);
        p.pull(190, 20, 200);
        CHECK(pz[1] >= 3 && pz[9] >= 7 && p.frames.back().state == "action" && p.count("pump_back") == 1,
              "pump shotgun keeps full-travel feel and reversal detent at mid-stroke");
        p.feed({205, 225, 245, 255}, 0, 200);
        CHECK(p.frames.back().state == "cycling" && p.count("pump_back") == 2, "pump shotgun cycles at zone 9");

        Rig vm(cat.resolve_name("WEAPON_RIFLE_VARMINT"));
        vm.hints.valid = true; vm.hints.hip_fire = false;
        vm.pull(255, 20, 200); vm.hints.ready = false; vm.release(35, 200);
        auto vz = fx::zones_of(vm.frames.back().r2);
        vm.pull(255, 20, 200);
        CHECK(vz[9] <= 4 && vz[9] < pz[9] && vm.count("slide_small_back") >= 2,
              "varmint slide is much lighter than pump shotgun and emits slide_small_back");
        CHECK(check_action_slips("WEAPON_SHOTGUN_PUMP", "pump_back", true) > 0 &&
                  check_action_slips("WEAPON_SHOTGUN_PUMP", "pump_back", false) == 0,
              "dirty pump respects HammerSlip setting");

        // double-action revolver: mid-stroke cylinder bump, condition stiffness, and ZERO firing slips
        Rig da_clean(cat.resolve_name("WEAPON_REVOLVER_DOUBLEACTION"));
        da_clean.hints.valid = true; da_clean.hints.hip_fire = false; da_clean.hints.grit = 0.0f;
        da_clean.feed({0});
        auto dac0 = fx::zones_of(da_clean.frames.back().r2);
        da_clean.pull(145, 10, 200);
        int bump_z5 = 0;
        for (const auto& fr : da_clean.frames)
            for (const auto& cu : fr.cues)
                if (cu.name == "cylinder_index") bump_z5 = fx::zones_of(fr.r2)[5];
        CHECK(da_clean.count("cylinder_index") == 1 && bump_z5 > dac0[5],
              "double-action revolver produces palpable mid-stroke cylinder_index bump");

        Rig da_dirty(cat.resolve_name("WEAPON_REVOLVER_DOUBLEACTION"));
        da_dirty.hints.valid = true; da_dirty.hints.hip_fire = false; da_dirty.hints.grit = 0.9f;
        da_dirty.feed({0});
        auto dad0 = fx::zones_of(da_dirty.frames.back().r2);
        CHECK(dad0[2] > dac0[2] && dad0[4] > dac0[4],
              "dirty double-action revolver pull becomes noticeably stiffer");

        Settings hs_on;
        hs_on.hammer_slip = true;
        Rig da_noslip(cat.resolve_name("WEAPON_REVOLVER_DOUBLEACTION"), hs_on);
        da_noslip.hints.valid = true; da_noslip.hints.hip_fire = false; da_noslip.hints.grit = 1.0f;
        for (int i = 0; i < 40; ++i) {
            da_noslip.hints.clip = 6;
            da_noslip.hints.ready = true;
            da_noslip.release(45, 200).pull(255, 20, 200);
        }
        CHECK(da_noslip.shots_rt() == 40, "dirty double-action revolver never slips on firing");
    }
    {   // fanning when hip firing
        Rig r(catt);
        r.pull().release(100).pull();
        CHECK(r.count("shot_revolver") == 2, "rapid hip-fire taps fan the hammer");
    }
    {   // lever action
        Rig r(cat.resolve_name("WEAPON_REPEATER_CARBINE"));
        r.pull().release(125).pull().release(80).pull();
        CHECK(r.count("lever_open") == 1 && r.count("lever_close") == 1 && r.count("shot_repeater") == 2, "lever cycles between shots");
    }
    {   // empty double barrel: dry click
        Rig r(cat.resolve_name("WEAPON_SHOTGUN_DOUBLEBARREL"));
        for (int i = 0; i < 2; ++i) r.pull().release(100);
        r.pull();
        CHECK(r.count("dry_fire") == 1, "dry click when empty");
    }
    {   // game data: reloading switches the trigger off; game clip 0 = empty
        Rig r(catt);
        r.hints.valid = true;
        r.hints.reloading = true;
        r.feed({0, 0});
        CHECK(r.frames.back().state == "reloading" && r.frames.back().r2[0] == fx::OFF, "game reload: trigger slack");
        r.hints.reloading = false;
        r.hints.clip = 0;
        r.feed({0}).pull();
        CHECK(r.count("dry_fire") == 1, "game says empty: dry click");
        // live reload emits begin gate, per-round ticks as clip grows, and end gate
        Rig rl(catt);
        rl.hints.valid = true;
        rl.hints.clip = 2;
        rl.feed({0});
        rl.hints.reloading = true;
        rl.feed({0});
        rl.hints.clip = 3;
        rl.feed({0});
        rl.hints.clip = 4;
        rl.feed({0});
        rl.hints.reloading = false;
        rl.feed({0});
        CHECK(rl.count("cylinder_gate") == 2 && rl.count("reload_round") == 2,
              "live reload emits cylinder gate and round ticks");
    }
    {   // fishing from the game's fishing task: bite on R2, line tension on L2, the fight, the catch
        Rig r(cat.resolve_name("WEAPON_FISHINGROD"));
        r.hints.valid = true;
        FishingState& fs = r.hints.fishing;
        fs.active = true;
        fs.state = 6;
        r.hold(0, 0.1);
        CHECK(r.frames.back().r2[0] != fx::OFF && r.count("fish_bite") == 0, "line in the water: R2 light, no bite yet");
        fs.weight_lb = 3.5f;
        r.hold(0, 0.1);
        CHECK(r.count("fish_bite") == 1 && r.frames.back().r2 == fx::vibration(2, 7, 9), "a fish takes the bait: R2 tugs");
        fs.state = 7;
        fs.tension = 0.5f;
        fs.struggle = 1;
        fs.side = 0.9f;
        r.hold(0, 1.0);
        bool right = false;
        for (auto& fr : r.frames) for (auto& c : fr.cues) right |= c.name == "fish_tug" && c.has_pan && c.pan > 0.5f;
        CHECK(r.count("fish_hooked") == 1 && r.frames.back().l2 == fx::feedback(0, 5), "hooked: L2 holds the line tension");
        CHECK(right, "the fish surges to its side (right)");
        fs.tension = 0.95f;
        r.hold(0, 0.05);
        CHECK(r.frames.back().l2 == fx::vibration(0, 8, 30), "about to snap: L2 strains");
        fs.state = 6;
        fs.weight_lb = 0;
        r.hold(0, 0.05);
        CHECK(r.count("line_snap") == 1, "snapped at high tension");
        fs.state = 7; fs.tension = 0.3f;
        r.hold(0, 0.3);
        fs.state = 12;
        r.hold(0, 2.0);
        CHECK(r.count("fish_landed") == 1 && r.count("fish_flop") >= 1, "landed: it flaps in your hand");
    }
    {   // Dead Eye: marks tick, and each shot of the volley kicks when the game fires it
        Rig r(cat.resolve_name("WEAPON_REVOLVER_CATTLEMAN"));
        r.hints.valid = true;
        r.hints.dead_eye = true;
        r.hints.clip = 6;
        r.hold(0, 0.1);
        r.hints.deadeye_marks = 1; r.hold(0, 0.1);
        r.hints.deadeye_marks = 2; r.hold(0, 0.1);
        CHECK(r.count("deadeye_mark") == 2, "two targets painted: two ticks");
        int before = r.count("shot_revolver");
        for (int i = 0; i < 3; ++i) {               // the game fires the volley by itself
            r.hints.clip -= 1;
            r.hints.shooting = true;
            r.hold(0, 0.02);
            r.hints.shooting = false;
            r.hold(0, 0.25);
        }
        CHECK(r.count("shot_revolver") - before == 3, "every volley shot kicks");
    }
    {   // ini numbers read the same whatever locale the game switches to (it uses a decimal comma)
        const std::string pid = std::to_string(GetCurrentProcessId());
        fs::path tmp = fs::temp_directory_path() / ("dsr2_locale_test_" + pid + ".ini");
        { std::ofstream o(tmp); o << "[Gyro]\nVerticalRatio=0.75\n[Voice]\nMinConfidence=0,7\n"; }
        const char* prev = setlocale(LC_ALL, nullptr);
        std::string saved = prev ? prev : "C";
        bool comma_locale = setlocale(LC_ALL, "Russian") != nullptr || setlocale(LC_ALL, "de_DE") != nullptr;
        Config cfg;
        cfg.load(tmp.wstring());
        CHECK(std::fabs(cfg.num("Gyro", "VerticalRatio", 9) - 0.75f) < 1e-4f &&
              std::fabs(cfg.num("Voice", "MinConfidence", 9) - 0.7f) < 1e-4f, "0.75 / 0,7 read right under a comma locale");
        if (!comma_locale) printf("  (note: comma locale not available, tested in the C locale)\n");
        setlocale(LC_ALL, saved.c_str());
        std::error_code ec;
        fs::remove(tmp, ec);
    }
    {   // the settings page writes the ini in place: comments and order kept, missing keys added, read back
        const std::string pid = std::to_string(GetCurrentProcessId());
        fs::path tmp = fs::temp_directory_path() / ("dsr2_set_test_" + pid + ".ini");
        { std::ofstream o(tmp); o << "; top comment\n[Haptics]\n; how strong\nFootsteps=0.22\n\n[Gyro]\nMode=off\n"; }
        Config cfg;
        cfg.load(tmp.wstring());
        const MenuEntry* steps = nullptr; const MenuEntry* gyro = nullptr; const MenuEntry* trig = nullptr;
        for (auto& e : SettingsMenu::entries()) {
            if (std::string(e.key) == "Footsteps") steps = &e;
            if (std::string(e.key) == "Mode") gyro = &e;
            if (std::string(e.section) == "Triggers" && std::string(e.key) == "Enabled") trig = &e;
        }
        CHECK(steps && gyro && trig, "settings page lists footsteps, gyro, triggers");
        CHECK(SettingsMenu::value_text(*steps, cfg) == "22%", "footsteps shows 22%");
        cfg.set("Haptics", "Footsteps", SettingsMenu::stepped(*steps, cfg, +1));
        CHECK(SettingsMenu::value_text(*steps, cfg) == "30%", "right: 22% -> 30% (on the 10% grid)");
        cfg.set("Gyro", "Mode", SettingsMenu::stepped(*gyro, cfg, +1));
        cfg.set("Triggers", "Enabled", SettingsMenu::stepped(*trig, cfg, -1));
        CHECK(cfg.str("Gyro", "Mode", "") == "aim" && !cfg.flag("Triggers", "Enabled", true), "choice and new section written");
        std::string all;
        {
            std::ifstream in(tmp);
            all.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }   // close handle before subsequent atomic MoveFileExW writes in cfg.set()
        CHECK(all.find("; top comment") != std::string::npos && all.find("; how strong") != std::string::npos &&
              all.find("Footsteps=0.3") != std::string::npos && all.find("[Triggers]") != std::string::npos,
              "comments kept, value replaced in place, section appended");
        for (int i = 0; i < 30; ++i) {
            std::string v = SettingsMenu::stepped(*steps, cfg, -1);
            if (v.empty()) break;
            cfg.set("Haptics", "Footsteps", v);
        }
        CHECK(SettingsMenu::value_text(*steps, cfg) == "Off", "left all the way: Off, then stops");
        std::error_code ec;
        fs::remove(tmp, ec);
    }
    {   // new defaults reach players who already have an ini: untouched old defaults move, changed ones stay, once
        const std::string pid = std::to_string(GetCurrentProcessId());
        fs::path tmp = fs::temp_directory_path() / ("dsr2_migrate_test_" + pid + ".ini");
        { std::ofstream o(tmp); o << "[General]\nDebug=0\n[Haptics]\nWeather=0.5\nGameRumble=0.35\n[Speaker]\nMechanics=1\n"
                                     "[Lightbar]\nAmmoLeds=0\n[Mic]\nCooldown=1.2\n"; }
        Config cfg;
        cfg.load(tmp.wstring());
        CHECK(cfg.str("Speaker", "Mechanics", "") == "0" && std::fabs(cfg.num("Haptics", "GameRumble", 9) - 0.15f) < 1e-4f,
              "untouched old defaults -> the new ones");
        CHECK(std::fabs(cfg.num("Haptics", "Weather", 9) - 0.5f) < 1e-4f, "a value the player changed stays");
        CHECK(cfg.str("Lightbar", "PlayerLeds", "") == "off" && std::fabs(cfg.num("Mic", "Cooldown", 9) - 3.0f) < 1e-4f,
              "AmmoLeds=0 -> PlayerLeds=off; whistle cooldown 1.2 -> 3");
        CHECK(cfg.integer("General", "DefaultsVersion", 0) == 3, "the file remembers it is up to date");
        cfg.set("Speaker", "Mechanics", "1");             // the player turns it back on later
        Config again;
        again.load(tmp.wstring());
        CHECK(again.str("Speaker", "Mechanics", "") == "1", "only once: a later choice of the old value stays");
        std::error_code ec;
        fs::remove(tmp, ec);
    }
    {   // the in-game page: toggles and choices = option steppers (index), levels = whole percents
        Config cfg;
        std::set<std::string> ids;
        const MenuEntry* steps = nullptr; const MenuEntry* strength = nullptr; const MenuEntry* gyro = nullptr;
        const MenuEntry* trig = nullptr;
        const MenuEntry* slip = nullptr; const MenuEntry* spk_amb = nullptr; const MenuEntry* spk_ui = nullptr;
        const MenuEntry* landing = nullptr; const MenuEntry* tod = nullptr; const MenuEntry* gyro_sens = nullptr;
        const MenuEntry* gyro_out = nullptr; const MenuEntry* voice_conf = nullptr;
        bool ok = true;
        for (auto& e : SettingsMenu::entries()) {
            if (e.kind == MenuEntry::Header) continue;
            ok = ok && ids.insert(e.id).second &&
                 (e.kind == MenuEntry::Number) == SettingsMenu::option_labels(e).empty();
            std::string id = e.id;
            if (id == "haptics_footsteps") steps = &e;
            if (id == "haptics_strength") strength = &e;
            if (id == "gyro_mode") gyro = &e;
            if (id == "triggers_enabled") trig = &e;
            if (id == "triggers_hammer_slip") slip = &e;
            if (id == "speaker_ambient") spk_amb = &e;
            if (id == "speaker_ui") spk_ui = &e;
            if (id == "haptics_landing") landing = &e;
            if (id == "lightbar_tod") tod = &e;
            if (id == "gyro_sensitivity") gyro_sens = &e;
            if (id == "gyro_output") gyro_out = &e;
            if (id == "voice_confidence") voice_conf = &e;
        }
        CHECK(ok && ids.size() == 63 && steps && strength && gyro && trig && slip && spk_amb && spk_ui &&
                  landing && tod && gyro_sens && gyro_out && voice_conf,
              "page rows: unique ids, all 63 settings present");
        CHECK(SettingsMenu::page_max(*steps) == 100 && SettingsMenu::page_max(*strength) == 200, "levels: 0 .. max %");
        CHECK(SettingsMenu::page_value(*steps, cfg) == 35, "footsteps default 0.35 shows as exactly 35%");
        CHECK(SettingsMenu::page_ini(*steps, 37) == "0.37" && SettingsMenu::page_ini(*strength, 200) == "2" &&
              SettingsMenu::page_ini(*steps, 0) == "0", "percent -> ini value");
        CHECK(SettingsMenu::page_ini(*steps, 101).empty() && SettingsMenu::page_ini(*steps, -1).empty(),
              "out of range -> nothing");
        CHECK(SettingsMenu::option_labels(*gyro) ==
                  std::vector<std::string>({"DS_GYRO_MODE_OFF", "DS_GYRO_MODE_AIM", "DS_GYRO_MODE_ALWAYS"}) &&
                  SettingsMenu::page_ini(*gyro, 1) == "aim" && SettingsMenu::page_ini(*gyro, 2) == "always" &&
                  SettingsMenu::page_ini(*gyro, 3).empty(),
              "choice options");
        CHECK(SettingsMenu::page_value(*trig, cfg) == 1 && SettingsMenu::page_ini(*trig, 0) == "0", "toggle 0 / 1");
        CHECK(SettingsMenu::page_value(*slip, cfg) == 1 && SettingsMenu::page_ini(*slip, 0) == "0" &&
                  SettingsMenu::page_value(*spk_amb, cfg) == 1 && SettingsMenu::page_value(*spk_ui, cfg) == 1 &&
                  SettingsMenu::page_value(*tod, cfg) == 1 && SettingsMenu::page_value(*landing, cfg) == 70 &&
                  SettingsMenu::page_max(*gyro_sens) == 200 && SettingsMenu::page_value(*gyro_sens, cfg) == 100 &&
                  SettingsMenu::page_value(*gyro_out, cfg) == 0 && SettingsMenu::page_ini(*gyro_out, 1) == "stick" &&
                  SettingsMenu::page_value(*voice_conf, cfg) == 70,
              "new ini rows exposed on page with expected types and defaults");
    }
    {   // double action: every quick press reaches the game, whatever its "ready to shoot" says
        Settings st; st.manual_action = false;
        Rig r(cat.resolve_name("WEAPON_REVOLVER_DOUBLEACTION"), st);
        r.hints.valid = true;
        r.hints.hip_fire = false;
        r.hints.ready = false;                      // false for 1.5 s and more after a shot in the real game
        r.pull(255, 80);
        r.release(150).pull(255, 80);
        r.release(150).pull(255, 80);
        CHECK(r.count("shot_revolver") == 3, "three quick presses, three shots, not ready or not");
        r.release(10).pull(190, 30).hold(190, 0.1);
        CHECK(r.count("shot_revolver") == 4, "a quick follow-up needs no full pull");
    }
    {   // from the hip and with two guns the game cycles single-action guns itself: no R2 stroke of ours
        for (int mode = 0; mode < 2; ++mode) {
            Rig r(cat.resolve_name("WEAPON_REVOLVER_CATTLEMAN"));
            r.hints.valid = true;
            r.hints.hip_fire = mode == 0;
            r.hints.dual_wield = mode == 1;
            r.hints.ready = false;
            r.pull(255, 80);
            r.release(40).pull(255, 80);
            CHECK(r.count("hammer_cock") == 0, "no hammer stroke from the hip / with two guns");
            CHECK(r.frames.back().state != "action" && r.frames.back().state != "cycling", "never waits for a stroke");
        }
    }
    {   // the game's readiness decides cocking: not ready -> the next stroke works the lever
        Rig r(cat.resolve_name("WEAPON_REPEATER_CARBINE"));
        r.hints.valid = true;
        r.hints.hip_fire = false;
        r.pull();
        r.hints.ready = false;
        r.release(40);
        CHECK(r.frames.back().state == "action", "not ready after a shot: lever stroke next");
        r.pull();
        CHECK(r.count("lever_open") == 1 && r.count("shot_repeater") == 1, "the stroke works the lever, no shot");
        r.hints.ready = true;                       // the game: lever closed, chambered
        r.release(20).pull();
        CHECK(r.count("shot_repeater") == 2, "ready again: fires");
    }
    {   // the game cocks it by itself (auto-cock mod, aiming): skip the stroke
        Rig r(cat.resolve_name("WEAPON_REVOLVER_CATTLEMAN"));
        r.hints.valid = true;
        r.hints.hip_fire = false;
        r.pull(255, 40, 200);
        r.hints.ready = false;
        r.release(40, 200);
        r.hints.ready = true;
        r.release(5, 200).pull(255, 40, 200);
        CHECK(r.count("hammer_cock") == 0 && r.count("shot_revolver") == 2, "game-cocked: fires without a stroke");
    }
    {   // in game the kick comes with the game's real shot; a press the game refuses gets no kick
        Rig r(cat.resolve_name("WEAPON_REVOLVER_CATTLEMAN"));
        r.hints.valid = true;
        r.game_fires = false;
        r.pull();
        r.hold(255, 2.0);
        CHECK(r.count("shot_revolver") == 0 && r.count("reset_click") >= 1, "refused shot: the sear clicks, no kick");
        Rig q(cat.resolve_name("WEAPON_REVOLVER_CATTLEMAN"));
        q.hints.valid = true;
        q.pull();
        q.hold(255, 0.3);
        CHECK(q.count("shot_revolver") == 1, "the game fires: the kick comes");
    }
    {   // recoil: a shove against the finger first, then the fading rumble
        Settings s; s.manual_action = false;
        Rig r(cat.resolve_name("WEAPON_SHOTGUN_DOUBLEBARREL"), s);
        r.pull().hold(255, 0.3);
        std::vector<uint8_t> modes;
        for (auto& f : r.frames) if (f.state == "held") modes.push_back(f.r2[0]);
        auto first_vib = std::find(modes.begin(), modes.end(), fx::VIBRATION);
        CHECK(first_vib != modes.end() && first_vib != modes.begin() && *(first_vib - 1) == fx::FEEDBACK,
              "shove (feedback) right before the rumble (vibration)");
        CHECK(std::count(modes.begin(), modes.end(), fx::VIBRATION) >= 25, "rumble lasts ~100 ms+");
    }
    {   // bow: progressive solid feedback ramp up to full 8/8 at draw, release vs letdown
        Rig r(cat.resolve_name("WEAPON_BOW"));
        r.feed({0});
        auto rz = fx::zones_of(r.frames.back().r2);
        CHECK(r.frames.back().r2[0] == fx::FEEDBACK && rz[1] >= 3 && rz[9] == 8,
              "bow ready ramps resistance across trigger travel");
        r.pull(200, 10).hold(200, 3.5);
        auto dz = fx::zones_of(r.frames.back().r2);
        CHECK(r.frames.back().r2[0] == fx::FEEDBACK && dz[2] == 8 && dz[9] == 8,
              "bow stays solid feedback at max 8/8 force at full draw");
        int bow_spk = 0;
        for (const auto& fr : r.frames)
            for (const auto& c : fr.cues)
                if (c.name == "bow_creak" && c.speaker) ++bow_spk;
        CHECK(bow_spk == 1, "bow_creak plays on the speaker only once per draw");
        r.release();
        CHECK(r.count("bow_release") == 1, "bow fires on release after a full draw");
        Rig e(cat.resolve_name("WEAPON_BOW"));
        e.pull(200, 10).hold(200, 0.1).release();
        CHECK(e.count("bow_letdown") == 1 && e.count("bow_release") == 0, "early bow release lets down without firing");
    }
    {   // lasso: spinning loop at natural cadence while L2+R2 held, throw on R2 release, ignore R2 until rope returns
        Rig r(cat.resolve_name("WEAPON_LASSO"));
        r.hints.valid = true;
        r.hints.hip_fire = false;
        r.hints.ready = true;
        r.pull(255, 10, 200).hold(255, 1.5, 200);
        CHECK(r.frames.back().state == "spinning" && r.count("lasso_spin") == 3 && r.count("lasso_throw") == 0,
              "lasso loops lasso_spin cleanly without overlapping itself while holding L2+R2");
        r.release(10, 200);
        CHECK(r.frames.back().state == "thrown" && r.count("lasso_throw") == 1,
              "releasing R2 throws the lasso and enters thrown recovery state");
        int spins_before = r.count("lasso_spin"), throws_before = r.count("lasso_throw");
        r.hints.ready = false;
        r.pull(255, 10, 200).hold(255, 0.25, 200).release(10, 200);
        CHECK(r.count("lasso_spin") == spins_before && r.count("lasso_throw") == throws_before,
              "pulling R2 before the rope returns does not play lasso_spin");
        r.hints.ready = true;
        r.hold(0, 0.30, 200);
        CHECK(r.frames.back().state == "ready", "lasso returns to ready once rope is back and R2 released");
        r.pull(255, 10, 200);
        CHECK(r.frames.back().state == "spinning" && r.count("lasso_spin") == spins_before + 1,
              "lasso spins cleanly again once ready");
    }
    {   // melee without the game: L2 = target
        nlohmann::json fist = cat.resolve_name("WEAPON_UNARMED");
        fist["melee"]["block_chance"] = 0;
        Rig r(fist);
        r.feed({0}, R1, 200).release(100, 200).feed({0}, R1, 200).release(100, 200).feed({0}, R1, 200).release(100, 200);
        CHECK(r.count("punch_finisher") == 1, "third hit in a row is a finisher");
        Rig m(fist);
        m.feed({0}, R1).release(100);
        CHECK(m.count("punch_miss") == 1, "swing at air is a miss");
    }
    {   // melee in game: R1 (cover/brake) does not swing; CIRCLE swings with completely free R2/L2 triggers
        Rig r(cat.resolve_name("WEAPON_UNARMED"));
        r.hints.valid = true;
        r.hints.hip_fire = false;
        r.feed({0}, R1).release(5);
        CHECK(r.count("punch_swing") == 0, "R1 does not fire phantom melee in game");
        r.feed({0}, CIRCLE).release(20);
        bool all_free = true;
        for (const auto& fr : r.frames) all_free = all_free && fr.r2 == fx::off() && fr.l2 == fx::off();
        r.hints.melee_hits = 1;
        r.release(5);
        for (const auto& fr : r.frames) all_free = all_free && fr.r2 == fx::off() && fr.l2 == fx::off();
        CHECK(r.count("punch_impact") + r.count("punch_impact_face") == 1 && all_free,
              "game damage confirms the punch and R2/L2 stay free when pressing Circle");

        Rig kf(cat.resolve_name("WEAPON_MELEE_KNIFE"));
        kf.hints.valid = true;
        kf.hints.hip_fire = false;
        kf.feed({0}, CIRCLE).release(20);
        kf.hints.melee_hits = 1;
        kf.release(5);
        bool knife_free = true;
        for (const auto& fr : kf.frames) knife_free = knife_free && fr.r2 == fx::off() && fr.l2 == fx::off();
        CHECK(kf.count("impact_blade") + kf.count("impact_blade_deep") == 1 && knife_free,
              "equipped knife/melee keeps R2 and L2 triggers completely free");
    }
    {   // DSP: converted gunshot keeps its energy where the actuators work
        std::vector<float> shot(44100 * 8 / 10);
        Rng rng(1);
        for (size_t i = 0; i < shot.size(); ++i) shot[i] = rng.gauss() * std::exp(-float(i) / 44100 / 0.08f);
        Stereo h = dsp::to_haptic(shot, 44100, dsp::preset("handgun"), 0.5f, 1.0f);
        size_t n = 1;
        while (n < h.size() / 2) n <<= 1;
        std::vector<std::complex<float>> a(n);
        for (size_t i = 0; i < h.size() / 2; ++i) a[i] = h[2 * i + 1];
        dsp::fft(a);
        double lo = 0, hi = 0;
        for (size_t k = 1; k < n / 2; ++k) (k * 48000.0 / n > 500 ? hi : lo) += std::abs(a[k]);
        CHECK(hi / (lo + hi) < 0.12, "haptic energy stays below 500 Hz");
        auto k = dsp::kick_steps(shot, 44100, 16);
        CHECK(!k.empty() && k[0] == 8 && std::is_sorted(k.rbegin(), k.rend()), "recoil envelope decays");
    }
    {   // recipes render; speaker plays real game recordings only (never synthesized beeps/clicks)
        HapticLibrary lib;
        lib.load(data / "haptics.json", nullptr);
        Stereo out;
        Cue c;
        c.name = "hammer_cock";
        CHECK(lib.render(c, out) && out.size() > 100, "recipe renders");
        CHECK(!lib.speaker_sound(c, out), "hammer_cock skips speaker when no real recording is present");
        CHECK(HapticLibrary::speaker_category("hammer_cock") == SpeakerCategory::Mech &&
              HapticLibrary::speaker_category("reload_round") == SpeakerCategory::Mech &&
              HapticLibrary::speaker_category("gun_draw") == SpeakerCategory::Mech &&
              HapticLibrary::speaker_category("gun_holster") == SpeakerCategory::Mech &&
              HapticLibrary::speaker_category("bow_release") == SpeakerCategory::Mech &&
              HapticLibrary::speaker_category("lasso_spin") == SpeakerCategory::Mech &&
              HapticLibrary::speaker_category("lasso_throw") == SpeakerCategory::Mech &&
              HapticLibrary::speaker_category("bow_creak") == SpeakerCategory::Mech &&
              HapticLibrary::speaker_category("impact_blade") == SpeakerCategory::Mech &&
              HapticLibrary::speaker_category("skin_cut") == SpeakerCategory::Ambient &&
              HapticLibrary::speaker_category("rain_drop") == SpeakerCategory::Ambient &&
              HapticLibrary::speaker_category("hail_drop") == SpeakerCategory::Ambient &&
              HapticLibrary::speaker_category("wheel_tick") == SpeakerCategory::UI &&
              HapticLibrary::speaker_category("ui_confirm") == SpeakerCategory::UI &&
              HapticLibrary::speaker_category("longarm_draw") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("longarm_holster") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("blade_draw") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("blade_holster") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("step_snow") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("step_gravel") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("step_mud") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("step_sand") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("step_wood") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("step_grass") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("step_ground") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("step_stone") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("step_water") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("wade_push") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("splash") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("brush") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("brush_in") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("reel_click") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("deadeye_mark") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("ui_select") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("ui_nav") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("honor_up") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("horse_hitch") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("wood_chop_impact") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("chips") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("domino_clack") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("fire_crackle") == SpeakerCategory::None &&
              HapticLibrary::speaker_category("shot_revolver") == SpeakerCategory::None,
              "speaker categories keep real recordings only and drop noisy footsteps/blades/longarms");
        for (const char* no_sr : {"hammer_cock", "reload_round", "cylinder_gate", "lever_open", "dry_fire",
                                  "reel_click", "deadeye_mark", "wheel_tick", "ui_confirm", "gun_draw",
                                  "bow_creak", "bow_release", "lasso_spin", "lasso_throw",
                                  "longarm_draw", "blade_draw", "step_ground", "step_water", "step_stone",
                                  "ui_select", "ui_nav", "wood_chop_impact", "horse_hitch", "chips",
                                  "fire_crackle"}) {
            Cue sc;
            sc.name = no_sr;
            CHECK(!lib.speaker_sound(sc, out), no_sr);
        }
        for (const char* r : {"hoof_dirt", "hoof_stone", "rail_clack", "hit_bullet", "gunfire_near", "shiver", "horse_heartbeat",
                              "land", "climb_grab", "door_open", "longarm_draw", "longarm_holster", "blade_draw", "blade_holster"}) {
            c.name = r;
            CHECK(lib.render(c, out), r);
        }
        // every surface the world can report has a footstep and a hoof recipe
        for (const auto& surf : Surfaces::labels()) {
            c.name = "step_" + surf;
            CHECK(lib.render(c, out), c.name.c_str());
            c.name = "hoof_" + surf;
            CHECK(lib.render(c, out), c.name.c_str());
        }
        for (const char* r : {"wade_push", "brush", "brush_in", "splash", "rain_drop", "explosion_fire", "explosion_small", "thunder_strike",
                              "swim_pull", "swim_kick", "horse_paddle", "water_entry"}) {
            c.name = r;
            CHECK(lib.render(c, out) && out.size() > 100, r);
        }
        // a blast rolls on after the bang: still felt 1.5 s in
        c.name = "explosion_roll";
        CHECK(lib.render(c, out) && out.size() / 2 > size_t(1.5 * 48000), "a blast lasts over 1.5 s");
        // "variants": every take of a material its own grain - no two steps on the same ground identical
        c.name = "step_gravel";
        lib.render(c, out);
        CHECK(lib.variant_count("step_gravel") == 4, "a material renders 4 variants");
        Stereo a, b;
        c.variant = 0;
        lib.render(c, a);
        c.variant = 1;
        lib.render(c, b);
        CHECK(!a.empty() && a != b, "variants differ");
        c.variant = -1;
    }
    {   // surface teaching: cycles labels, persists, context fallbacks
        const std::string pid = std::to_string(GetCurrentProcessId());
        fs::path tmp = fs::temp_directory_path() / ("dsr2_materials_test_" + pid + ".json");
        std::error_code ec;
        fs::remove(tmp, ec);
        Surfaces s;
        s.load(tmp);
        CHECK(s.classify(0x1234, false, 0, 0) == "ground", "unknown ground outdoors");
        CHECK(s.classify(0x1234, true, 0, 0) == "wood", "indoors = floorboards");
        CHECK(s.classify(0x1234, false, 0.3f, 0) == "water", "wading = water");
        CHECK(s.classify(0x1234, false, 0, 0.8f) == "snow", "snow on the ground");
        CHECK(s.teach(0x1234, "ground") == "grass" && s.teach(0x1234, "grass") == "mud", "F10 cycles labels");
        Surfaces again;
        again.load(tmp);
        CHECK(again.classify(0x1234, true, 0, 0) == "mud", "taught label saved and wins over context");
        fs::remove(tmp, ec);
    }
    {   // ground materials: joaat("<name>_<line in materials.dat>")
        CHECK(joaat("default_0") == 0x962C3F7B && joaat("concrete_1") == 0x46CA81E8, "material hash scheme (GTA V pairs)");
        struct { uint32_t h; const char* name; const char* label; } seen[] = {
            {0x962C3F7B, "default_0", "ground"}, {0x15620571, "gravel_small_26", "gravel"},
            {0xB760A3D3, "gravel_dirt_32", "gravel"}, {0x4FB3BD53, "gravel_grass_33", "gravel"},
            {0x98D6E803, "dirt_track_34", "ground"}, {0xB77A074A, "dirt_grass_36", "ground"},
            {0x99F95379, "mud_hard_37", "ground"}, {0x4FD0CB17, "mud_soft_39", "mud"},
            {0x7A630E3B, "mud_deep_41", "mud"}, {0x1C028F83, "clay_hard_46", "ground"},
            {0x8FB48DA9, "grass_dead_53", "grass"}, {0x9AA99348, "grass_56", "grass"},
            {0xDD7EE301, "grass_short_57", "grass"}, {0x68758D7D, "pine_needles_63", "grass"},
            {0x8EA8779E, "leaves_dry_64", "grass"}, {0x48F70312, "wood_roof_91", "wood"},
            {0x1CC71ECE, "wood_solid_medium_96", "wood"},
        };
        int ok = 0;
        for (auto& e : seen) {
            std::string n = Surfaces::identify(e.h);
            if (n == e.name && Surfaces::label_for(n) == e.label) ++ok;
            else LOGW("material 0x%08X: got '%s' (%s), want %s", e.h, n.c_str(), Surfaces::label_for(n).c_str(), e.name);
        }
        CHECK(ok == int(std::size(seen)), "the 17 materials seen in game are identified and labelled");
        double t1 = now_s();
        CHECK(Surfaces::identify(0x00001234).empty(), "an unknown hash stays unknown");
        LOGI("material search: %.1f ms per unknown hash", (now_s() - t1) * 1000);
        CHECK(Surfaces::label_for("mud_underwater_40") == "water" && Surfaces::label_for("snow_loose_20") == "snow" &&
              Surfaces::label_for("sandstone_9") == "stone" && Surfaces::label_for("sand_wet_15") == "sand" &&
              Surfaces::label_for("carpet_fabric_80") == "ground", "material names -> surfaces");
        Surfaces s;
        s.classify(0x4FD0CB17, false, 0, 0);                // first sight: identified in the background
        for (int i = 0; i < 200 && s.name_of(0x4FD0CB17).empty(); ++i) Sleep(10);
        CHECK(s.classify(0x4FD0CB17, false, 0, 0) == "mud", "mud_soft under the boots = mud");
        CHECK(s.classify(0x4FD0CB17, false, 0, 0.8f) == "snow", "snow on top of it = snow");
    }
    {   // DualSense trigger power-reduction, speaker mute, Catalog/Engine, and Haptic/LED features
        DualSense ds;
        OutputState os;
        os.trigger_power_reduction = 5;
        os.speaker_enabled = false;
        uint8_t pl[47] = {};
        ds.encode_payload(os, pl);
        CHECK(pl[36] == 5, "trigger_power_reduction written to the low nibble of byte 36");
        CHECK((pl[0] & 0x20) && pl[5] == 0x00 && pl[7] == 0x00,
              "speaker_enabled=false mutes hardware speaker volume (0x00) and disables internal speaker path");
        os.speaker_enabled = true;
        ds.encode_payload(os, pl);
        CHECK((pl[0] & 0x20) && pl[5] == 0x64 && pl[7] == 0x30,
              "speaker_enabled=true enables internal speaker path (0x30) and volume (0x64)");
        uint8_t sample[4] = {0x31, 0x00, 0x10, 0x00};
        CHECK(DualSense::bt_crc32(sample, 4) != 0, "bt_crc32 computes non-zero CRC");
        // Davy lantern tool class + KickStrength=0 recoil suppression
        std::string davy_name;
        CHECK(cat.resolve(joaat("WEAPON_MELEE_DAVY_LANTERN"), 0, davy_name).value("type", "") == "tool" &&
                  davy_name == "WEAPON_MELEE_DAVY_LANTERN",
              "WEAPON_MELEE_DAVY_LANTERN resolves as tool");
        {
            Settings ks0;
            ks0.manual_action = false;
            ks0.kick_scale = 0.0f;
            Rig rk(catt, ks0);
            rk.pull().hold(255, 0.08);
            bool any_kick = false;
            for (const auto& f : rk.frames)
                if (f.r2[0] == fx::VIBRATION) any_kick = true;
            CHECK(!any_kick && rk.shots_rt() == 1, "KickStrength=0 emits shot without trigger recoil vibration");
        }
        // environmental, mount, pluck, blast trigger shock, health/wanted LEDs, and nearby trains
        HapticLibrary hlib;
        hlib.load(data / "haptics.json", nullptr);
        CHECK(hlib.has("hail_drop") && hlib.has("wind_gust") && hlib.has("thunder_roll") &&
              hlib.has("train_pass") && hlib.has("carry_shift") && hlib.has("pluck") &&
              hlib.has("horse_mount") && hlib.has("wagon_mount") &&
              hlib.has("step_wood") && hlib.has("step_stone") && hlib.has("hoof_wood") && hlib.has("hoof_stone"),
              "environmental, mount, pluck, and terrain haptic recipes loaded");
        CHECK(!hlib.has("holster_draw") &&
              !hlib.has("wood_chop_swing") && !hlib.has("wood_chop_impact") && !hlib.has("wood_stack") &&
              !hlib.has("fire_crackle") && !hlib.has("wagon_bump") && !hlib.has("slip_slide") &&
              !hlib.has("horse_hitch") && !hlib.has("ui_select") && !hlib.has("ui_nav") &&
              !hlib.has("ui_back") && !hlib.has("ui_tab"),
              "removed/orphan recipes are absent from haptics.json");
        {
            std::vector<float> sig(size_t(dsp::kSR) * 3);
            for (size_t i = 0; i < sig.size(); ++i) {
                float t = float(i) / float(dsp::kSR);
                sig[i] = 0.5f * std::sin(2 * kPi * 50.0f * t) + 0.4f * std::sin(2 * kPi * 160.0f * t);
            }
            auto peak_of = [](const std::vector<float>& buf) {
                float m = 0.0f;
                for (float s : buf) m = std::max(m, std::fabs(s));
                return m;
            };
            auto hs = dsp::to_haptic(sig, dsp::kSR, dsp::preset("surface"), 0.0f, 0.8f);
            auto hd = dsp::to_haptic(sig, dsp::kSR, dsp::preset("drop"), 0.0f, 0.8f);
            auto ht = dsp::to_haptic(sig, dsp::kSR, dsp::preset("thunder"), 0.0f, 0.9f);
            CHECK(hs.size() / 2 > 0 && hs.size() / 2 <= size_t(dsp::kSR * 0.20f) && peak_of(hs) > 0.1f,
                  "dsp::preset(\"surface\") renders crisp <=200ms haptic buffer");
            CHECK(hd.size() / 2 > 0 && hd.size() / 2 <= size_t(dsp::kSR * 0.07f) && peak_of(hd) > 0.1f,
                  "dsp::preset(\"drop\") renders crisp <=70ms haptic buffer");
            CHECK(ht.size() / 2 >= size_t(dsp::kSR * 0.50f) && ht.size() / 2 <= size_t(dsp::kSR * 2.25f) && peak_of(ht) > 0.1f,
                  "dsp::preset(\"thunder\") renders deep rolling 0.5-2.25s haptic buffer");
            const char* upgraded[] = {
                "thunder_roll", "thunder_strike", "rain_drop", "hail_drop",
                "step_ground", "step_grass", "step_mud", "step_stone", "step_gravel",
                "step_wood", "step_snow", "step_sand", "step_water", "wade_push", "splash",
                "brush", "brush_in", "gun_draw", "gun_holster",
                "longarm_draw", "longarm_holster", "blade_draw", "blade_holster",
                "lasso_spin", "ui_confirm"
            };
            bool all_ok = true;
            for (const char* name : upgraded) {
                Stereo buf;
                if (!hlib.has(name) || !hlib.render(Cue{name, 1.0f}, buf) || peak_of(buf) <= 0.01f) all_ok = false;
            }
            CHECK(all_ok, "all upgraded environmental, terrain, water, and draw/holster recipes render in offline fallback mode");
        }
        Features feat;
        feat.configure();
        InputState in; in.t = 10.0;
        GameSnapshot gs; gs.valid = true; gs.blocked = false; gs.halted = false;
        gs.health_core = 100;
        OutputState out;
        std::vector<Cue> cues;
        std::vector<WorldEvent> evs = {{WorldEvent::Life, 0.35f, 1.0f, "pluck"},
                                       {WorldEvent::Horse, 0.0f, 1.0f, "mount"},
                                       {WorldEvent::Life, 0.0f, 1.0f, "wagon_mount"},
                                       {WorldEvent::Explosion, 0.0f, 0.9f, "blast"}};
        feat.update(in, gs, evs, nullptr, false, -1, 0, false, out, cues);
        bool has_pl = false, has_hm = false, has_wm = false;
        for (const auto& c : cues) {
            if (c.name == "pluck") has_pl = true;
            if (c.name == "horse_mount") has_hm = true;
            if (c.name == "wagon_mount") has_wm = true;
        }
        CHECK(has_pl && has_hm && has_wm, "pluck, horse mount, and wagon mount emit dedicated cues");
        CHECK(out.r2[0] == fx::VIBRATION && out.l2[0] == fx::VIBRATION,
              "close blast applies brief shudder to both triggers");
        CHECK(out.player_leds == 0, "health LEDs stay off when health has not changed");
        // when health changes, show LEDs for 3.5 s, then turn off again
        gs.health_core = 60;
        in.t = 10.5; cues.clear(); out = OutputState{};
        feat.update(in, gs, {}, nullptr, false, -1, 0, false, out, cues);
        CHECK(out.player_leds == 0x15, "health change lights up player LEDs (60% = 3 LEDs)");
        in.t = 14.5; cues.clear(); out = OutputState{};
        feat.update(in, gs, {}, nullptr, false, -1, 0, false, out, cues);
        CHECK(out.player_leds == 0, "health LEDs turn off again 3.5 s after health change");
        // wanted: police-light LED sweep + escalating heartbeat
        gs.wanted = true; gs.wanted_score = 400;
        cues.clear(); out = OutputState{};
        feat.update(in, gs, {}, nullptr, false, -1, 0, false, out, cues);
        CHECK(out.player_leds == 0x03 || out.player_leds == 0x18, "wanted mode animates police-light player LEDs");
        bool has_hb = false;
        for (const auto& c : cues) if (c.name == "heartbeat") has_hb = true;
        CHECK(has_hb, "wanted mode emits tension heartbeat");
        // nearby moving train (monotonically increasing timestamps)
        gs.wanted = false;
        gs.near_train_dist = 10.0f; gs.near_train_speed = 12.0f; gs.near_train_pan = 0.5f;
        in.t = 15.0; cues.clear(); out = OutputState{};
        feat.update(in, gs, {}, nullptr, false, -1, 0, false, out, cues);
        in.t = 15.05;
        feat.update(in, gs, {}, nullptr, false, -1, 0, false, out, cues);
        bool has_train = false;
        for (const auto& c : cues) {
            if (c.name == "train_pass") has_train = true;
        }
        CHECK(has_train, "nearby moving train emits directional haptics");
        // LedMode::Off outputs 0 and Lightbar.Enabled=0 keeps writing black (never stuck on flash)
        const std::string pid = std::to_string(GetCurrentProcessId());
        fs::path tmp_ini = fs::temp_directory_path() / ("dsr2_feat_off_" + pid + ".ini");
        { std::ofstream o(tmp_ini); o << "[Lightbar]\nEnabled=0\nPlayerLeds=off\n"; }
        config().load(tmp_ini.wstring());
        Features feat_off;
        feat_off.configure();
        in.t = 16.0; cues.clear(); out = OutputState{};
        feat_off.update(in, gs, {}, nullptr, false, -1, 0, false, out, cues);
        CHECK(out.player_leds == 0 && out.has_lightbar && out.r == 0 && out.g == 0 && out.b == 0,
              "LedMode::Off forces player_leds=0 and Lightbar.Enabled=0 outputs black");
        in.t = 16.5; cues.clear(); out = OutputState{};
        feat_off.update(in, gs, {}, nullptr, false, -1, 0, false, out, cues);
        CHECK(out.has_lightbar && out.r == 0 && out.g == 0 && out.b == 0,
              "Lightbar.Enabled=0 keeps writing off so external flashes never stick");
        { std::ofstream o(tmp_ini); o << Config::default_text(); }
        config().load(tmp_ini.wstring());
        std::error_code ec;
        fs::remove(tmp_ini, ec);
    }
    {   // the XInput hook, exactly as the game calls it
        CHECK(input().install_hook(), "hook xinput9_1_0");
        using GetFn = DWORD(WINAPI*)(DWORD, XINPUT_STATE*);
        using SetFn = DWORD(WINAPI*)(DWORD, XINPUT_VIBRATION*);
        HMODULE x = GetModuleHandleW(L"xinput9_1_0.dll");
        auto get = x ? reinterpret_cast<GetFn>(GetProcAddress(x, "XInputGetState")) : nullptr;
        auto set = x ? reinterpret_cast<SetFn>(GetProcAddress(x, "XInputSetState")) : nullptr;
        CHECK(x && get && set, "xinput9_1_0 exports resolved");
        if (get && set) {
            InputState s;
            s.buttons = CROSS | DPAD_LEFT;
            s.r2 = 120;
            s.l2 = 77;
            s.lx = 255;
            s.ly = 0;
            input().publish(s, 255, true);
            XINPUT_STATE st{};
            CHECK(get(0, &st) == ERROR_SUCCESS, "pad 0 connected");
            CHECK((st.Gamepad.wButtons & XINPUT_GAMEPAD_A) && (st.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT), "buttons mapped");
            CHECK(st.Gamepad.bRightTrigger == 255 && st.Gamepad.bLeftTrigger == 77, "RT from the engine, LT raw");
            CHECK(st.Gamepad.sThumbLX == 32767 && st.Gamepad.sThumbLY > 32000, "stick up/right full range");
            CHECK(get(1, &st) == ERROR_DEVICE_NOT_CONNECTED, "other pads hidden");
            input().inject(XINPUT_GAMEPAD_DPAD_UP, 0.2);
            input().publish(s, -1, true);
            get(0, &st);
            CHECK((st.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) && st.Gamepad.bRightTrigger == 120, "whistle injection, RT passthrough");
            XINPUT_VIBRATION v{65535, 32768};
            CHECK(set(0, &v) == ERROR_SUCCESS && input().rumble_low() > 0.99f && input().rumble_high() > 0.49f, "game rumble captured");
        }
    }
    {   // Clean RAII teardown of thread-owning components (never std::terminate on scope exit) and stalled-script fallback
        {
            DualSense ds;
            AudioEngine ae;
            Dialogue dlg;
            Voice vc;
            Core cr;
            Surfaces sf;
            sf.classify(0x8FB48DA9, false, 0, 0);   // starts worker thread; ~Surfaces() joins cleanly on scope exit
        }
        CHECK(true, "thread-owning subsystems construct and destruct cleanly");
        CHECK(!Core::should_fallback_rumble(false, false, 10.0, 1.0f, 1.0f, false), "no fallback rumble when live");
        CHECK(!Core::should_fallback_rumble(true, true, 10.0, 1.0f, 1.0f, true), "no fallback rumble while wheel paused");
        CHECK(!Core::should_fallback_rumble(true, false, 2.0, 1.0f, 1.0f, false), "no fallback rumble before 3 s stall threshold");
        CHECK(!Core::should_fallback_rumble(true, false, 10.0, 0.0f, 0.0f, false), "no fallback rumble in silent pause menu");
        CHECK(Core::should_fallback_rumble(true, false, 3.5, 0.4f, 0.0f, false), "fallback activates on active game rumble after 3 s stall");
        CHECK(Core::should_fallback_rumble(true, false, 5.0, 0.0f, 0.0f, true), "fallback remains armed across brief 0/0 gaps");

        // UI Cross confirm gating
        CHECK(!Core::allow_ui_confirm(false, 0.0, false, false, false, true), "Cross ignored before string table is loaded");
        CHECK(!Core::allow_ui_confirm(false, 0.0, false, true, false, true), "Cross ignored during intro before main menu is ready");
        CHECK(Core::allow_ui_confirm(false, 0.0, false, true, true, true), "Cross fires once on main menu Story selection");
        CHECK(!Core::allow_ui_confirm(false, 0.0, false, true, true, false), "Cross stays disarmed on loading screen after clicking Story");
        CHECK(!Core::allow_ui_confirm(false, 5.0, false, true, true, true), "Cross ignored during initial story loading ticks");
        CHECK(Core::allow_ui_confirm(true, 100.0, true, true, true, false), "Cross fires in in-game pause menu");
        CHECK(!Core::allow_ui_confirm(true, 100.0, false, true, true, false), "Cross ignored during in-game loading / fading screens");
    }
    {   // Gyro bias stillness learning, soft deadzone, and post-shot pitch recoil gate
        const std::string pid = std::to_string(GetCurrentProcessId());
        fs::path tmp_ini = fs::temp_directory_path() / ("dsr2_gyro_" + pid + ".ini");
        {
            std::ofstream o(tmp_ini);
            o << "[Gyro]\nMode=aim\nOutput=stick\nSensitivity=1.0\nVerticalRatio=1.0\nSmoothThreshold=6.0\n";
        }
        config().load(tmp_ini.wstring());
        input().configure();
        input().reset_gyro_state();

        InputState gs;
        gs.fresh = true;
        double t = 1.0;
        auto step_gyro = [&](float pitch_dps, bool aiming) {
            t += 0.01;
            gs.t = t;
            gs.gyro[0] = int16_t(std::lround(pitch_dps * 16.4f));
            gs.gyro[1] = 0;
            gs.gyro[2] = 0;
            input().track_gyro(gs, aiming, true);
        };
        // 1.5 s resting at +1.8 deg/s residual bias -> must learn and cancel drift cleanly while aiming
        for (int i = 0; i < 150; ++i) step_gyro(1.8f, false);
        for (int i = 0; i < 60; ++i) step_gyro(1.8f, true);
        CHECK(std::fabs(input().gyro_ry()) < 1e-4f, "resting stillness cancels residual gyro drift");

        // 0.8 s of slow varying hand motion must NOT poison bias
        for (int i = 0; i < 80; ++i) step_gyro(2.0f + 3.0f * (float(i % 20) / 19.0f), false);
        for (int i = 0; i < 60; ++i) step_gyro(1.8f, true);
        CHECK(std::fabs(input().gyro_ry()) < 1e-4f, "slow hand motion does not poison learned gyro bias");

        // Deliberate +18 deg/s pitch must respond cleanly while aiming
        for (int i = 0; i < 25; ++i) step_gyro(18.0f, true);
        CHECK(input().gyro_ry() > 0.2f, "deliberate pitch rotation responds while aiming");

        CHECK(!World::allow_gyro_pitch(10.1, 10.4, 2.0f), "camera pitch muted immediately after shot");
        CHECK(!World::allow_gyro_pitch(10.5, 10.4, 0.0005f), "micro camera pitch below threshold suppressed");
        CHECK(World::allow_gyro_pitch(10.5, 10.4, 0.05f), "camera pitch resumes cleanly after recoil window");

        { std::ofstream o(tmp_ini); o << Config::default_text(); }
        config().load(tmp_ini.wstring());
        input().configure();
        input().reset_gyro_state();
        std::error_code ec;
        fs::remove(tmp_ini, ec);
    }
    {   // 9-feature suite: fx::scale, Touchpad/Triggers.Strength UI, EN/DE language detection,
        // Eagle Eye, Lit Fuse, Quick-Draw R2 Tension, Progressive Gun Cleaning, Boat Rowing,
        // Horse Spook & Wagon Braking.
        const auto fb = fx::multi_feedback({0, 2, 4, 6, 8, 0, 0, 0, 0, 0});
        CHECK(fx::scale(fb, 0.0f)[0] == fx::OFF, "fx::scale(0) disables trigger resistance");
        const auto fb_half = fx::zones_of(fx::scale(fb, 0.5f));
        CHECK(fb_half[1] == 1 && fb_half[3] == 3 && fb_half[4] == 4 && fb_half[5] == 0,
              "fx::scale(0.5) scales feedback zones cleanly and preserves empty zones");
        const auto fb_double = fx::zones_of(fx::scale(fb, 2.0f));
        CHECK(fb_double[1] == 4 && fb_double[3] == 8 && fb_double[4] == 8,
              "fx::scale(2.0) amplifies and clamps feedback zones to 8");
        const auto vib_half = fx::scale(fx::vibration(2, 6, 42), 0.5f);
        CHECK(vib_half[0] == fx::VIBRATION && vib_half[9] == 42 && fx::zones_of(vib_half)[2] == 3,
              "fx::scale scales vibration amplitude and preserves frequency");
        const auto wep_half = fx::scale(fx::weapon(3, 6, 8), 0.5f);
        CHECK(wep_half[0] == fx::WEAPON && wep_half[3] == 3, "fx::scale scales WEAPON mode strength");
        const auto gal = fx::galloping(3, 9, 4, 7, 5);
        CHECK(fx::scale(gal, 0.5f) == gal, "fx::scale leaves GALLOPING effect intact");

        // Language auto-detection, overrides, English fallback & translations.json coverage
        CHECK(detect_lang_from_probes("Ein", "Aus", "Zur\xC3\xBC" "ck", "Ausw\xC3\xA4hlen") == "de", "DE probe detection");
        CHECK(detect_lang_from_probes("Activ\xC3\xA9", "D\xC3\xA9sactiv\xC3\xA9", "Retour", "S\xC3\xA9lectionner") == "fr",
              "FR probe detection");
        CHECK(detect_lang_from_probes("S\xC3\xAD", "No", "Volver", "Seleccionar") == "es", "ES probe detection");
        CHECK(detect_lang_from_probes("S\xC3\xAC", "No", "Indietro", "Seleziona") == "it", "IT probe detection");
        CHECK(detect_lang_from_probes("Sim", "N\xC3\xA3o", "Voltar", "Selecionar") == "pt", "PT probe detection");
        CHECK(detect_lang_from_probes("W\xC5\x82.", "Wy\xC5\x82.", "Wstecz", "Wybierz") == "pl", "PL probe detection");
        CHECK(detect_lang_from_probes("\xD0\x92\xD0\xBA\xD0\xBB.", "\xD0\x92\xD1\x8B\xD0\xBA\xD0\xBB.",
                                      "\xD0\x9D\xD0\xB0\xD0\xB7\xD0\xB0\xD0\xB4",
                                      "\xD0\x92\xD1\x8B\xD0\xB1\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C") == "ru",
              "RU probe detection");
        CHECK(detect_lang_from_probes("On", "Off", "Back", "Select") == "en", "EN probe detection");
        CHECK(detect_lang_from_probes("\xE3\x82\xAA\xE3\x83\xB3", "\xE3\x82\xAA\xE3\x83\x95",
                                      "\xE6\x88\xBB\xE3\x82\x8B", "\xE9\x81\xB8\xE6\x8A\x9E") == "en",
              "Unsupported language (JP) falls back to English");
        CHECK(game_lang_code(0) == "en" && game_lang_code(1) == "fr" && game_lang_code(2) == "de" &&
                  game_lang_code(3) == "it" && game_lang_code(4) == "es" && game_lang_code(5) == "pt" &&
                  game_lang_code(6) == "pl" && game_lang_code(7) == "ru" && game_lang_code(11) == "es" &&
                  game_lang_code(8) == "en" && game_lang_code(10) == "en" && game_lang_code(99) == "en",
              "GET_CURRENT_LANGUAGE mapping and English fallback for unsupported game languages");
        CHECK(parse_language_override("auto").empty() && parse_language_override("Deutsch") == "de" &&
                  parse_language_override("pt-br") == "pt" && parse_language_override("unknown_xyz") == "en",
              "General.Language override parser");
        {
            std::ifstream trf(data / "translations.json");
            nlohmann::json tr = nlohmann::json::parse(trf, nullptr, false);
            CHECK(tr.is_object(), "translations.json parses cleanly");
            std::vector<std::string> req_keys = {"DUALSENSE_SETTINGS", "DUALSENSE_SETTINGS_DESC", "DS_PAGE_NOT_READY"};
            for (const auto& e : SettingsMenu::entries()) {
                req_keys.push_back(e.label_key);
                if (e.kind == MenuEntry::Header) continue;
                if (e.help && e.help[0]) req_keys.push_back(std::string(e.label_key) + "_DESC");
                for (const auto& ch : e.choices) req_keys.push_back(ch.label);
            }
            for (const char* code : {"de", "fr", "es", "it", "pt", "pl", "ru"}) {
                std::map<std::string, std::string> dict;
                if (tr.is_object() && tr.contains(code) && tr[code].is_object())
                    for (auto& [k, v] : tr[code].items())
                        if (v.is_string()) dict[k] = v.get<std::string>();
                size_t missing = 0;
                for (const auto& k : req_keys)
                    if (resolve_translation(dict, k, "__MISSING__") == "__MISSING__") ++missing;
                CHECK(missing == 0, (std::string("translations.json has complete key coverage for ") + code).c_str());
            }
            CHECK(resolve_translation({}, "DS_TRIGGERS_ENABLED", "Adaptive Triggers") == "Adaptive Triggers",
                  "missing/unsupported language resolves directly to English fallback");
        }

        // In-game Triggers.Strength + Touchpad gesture rows
        const MenuEntry* trig_str = nullptr;
        int tp_rows = 0;
        const MenuEntry* tp_up = nullptr;
        for (const auto& e : SettingsMenu::entries()) {
            if (std::string(e.id) == "triggers_strength") trig_str = &e;
            if (std::string(e.id).rfind("touchpad_", 0) == 0) ++tp_rows;
            if (std::string(e.id) == "touchpad_up") tp_up = &e;
        }
        CHECK(trig_str && SettingsMenu::page_max(*trig_str) == 200 && tp_rows == 6 && tp_up != nullptr,
              "Triggers.Strength and all 6 Touchpad gesture rows exist in SettingsMenu");
        {
            const std::string pid = std::to_string(GetCurrentProcessId());
            fs::path tp_ini = fs::temp_directory_path() / ("dsr2_tp_" + pid + ".ini");
            { std::ofstream o(tp_ini); o << "[Touchpad]\nClick=BACK\nSwipeUp=\n"; }
            Config tpc;
            tpc.load(tp_ini.wstring());
            if (tp_up) {
                CHECK(SettingsMenu::page_value(*tp_up, tpc) == 0 &&
                          SettingsMenu::value_text(*tp_up, tpc) == "Off" &&
                          SettingsMenu::page_ini(*tp_up, 0) == "OFF" &&
                          xbutton_from_name("OFF") == 0 &&
                          xbutton_from_name("BACK") == XINPUT_GAMEPAD_BACK,
                      "Touchpad empty INI value maps to Off cleanly and presets map to XInput buttons");
            }
            std::error_code ec;
            fs::remove(tp_ini, ec);
        }

        // Lit Fuse haptics + lightbar on Dynamite / Fire Bottle (NOT Poison Bottle)
        {
            Calibration cal;
            Settings s;
            Rig dyn(cat.resolve_name("WEAPON_THROWN_DYNAMITE"), s);
            dyn.hints.valid = true;
            dyn.hints.hip_fire = true;
            dyn.hold(80, 0.35, 200);
            bool saw_fuse = false, saw_crackle = false, cr_right = true;
            float last_fuse = 0;
            for (const auto& fr : dyn.frames) {
                if (fr.fuse > 0.01f) { saw_fuse = true; last_fuse = fr.fuse; }
                for (const auto& c : fr.cues) {
                    if (c.name == "fuse_crackle") {
                        saw_crackle = true;
                        if (!c.has_pan || c.pan < 0.4f) cr_right = false;
                    }
                }
            }
            CHECK(saw_fuse && last_fuse > 0.25f && saw_crackle && cr_right,
                  "lit dynamite fuse emits right-biased crackle cues and fuse urgency");
            Rig psn(cat.resolve_name("WEAPON_THROWN_POISONBOTTLE"), s);
            psn.hints.valid = true;
            psn.hold(80, 0.25, 200);
            bool psn_fuse = false;
            for (const auto& fr : psn.frames)
                if (fr.fuse > 0.01f || std::any_of(fr.cues.begin(), fr.cues.end(),
                                                   [](const Cue& c) { return c.name == "fuse_crackle"; }))
                    psn_fuse = true;
            CHECK(!psn_fuse, "poison bottle does not trigger lit-fuse crackle");
        }

        // Quick-Draw / Duel R2 tension (unarmed with can_quickdraw vs without, Circle remains loose)
        {
            Calibration cal;
            Settings s;
            Rig qd(cat.resolve_name("WEAPON_UNARMED"), s);
            qd.hints.valid = true;
            qd.hints.can_quickdraw = true;
            qd.feed({0, 0}, CIRCLE, 0);
            CHECK(qd.frames.back().r2[0] == fx::OFF, "quick-draw keeps Circle melee R2 trigger loose");
            qd.hold(0, 0.70, 0);
            qd.pull(180, 35, 0);
            qd.hold(180, 0.40, 0);
            const auto z = fx::zones_of(qd.frames.back().r2);
            CHECK(qd.frames.back().r2[0] == fx::FEEDBACK && z[7] == 8 && z[9] == 0 &&
                      qd.frames.back().game_rt == -1,
                  "quick-draw half-press builds rising wall at zone 7 while passing analog R2 through");
            CHECK(qd.count("quickdraw_tension") > 0, "quick-draw hold emits subtle charge pulses");
            qd.pull(250, 10, 0);
            CHECK(qd.count("reset_click") > 0, "pulling past quick-draw wall clicks at break");

            Rig no_qd(cat.resolve_name("WEAPON_UNARMED"), s);
            no_qd.hints.valid = true;
            no_qd.hints.can_quickdraw = false;
            no_qd.hold(160, 0.10, 0);
            CHECK(no_qd.frames.back().r2[0] == fx::OFF, "unarmed R2 stays loose when quick-draw is unavailable");
        }

        // Eagle Eye, Progressive Gun Cleaning, Boat Rowing, Horse Spook, Wagon Braking
        {
            Features f2;
            f2.configure();
            InputState in2; in2.t = 20.0;
            GameSnapshot g2; g2.valid = true; g2.blocked = false; g2.halted = false; g2.health_core = 100;
            OutputState out2;
            std::vector<Cue> cues2;
            auto has_cue = [&](const std::string& n) {
                return std::any_of(cues2.begin(), cues2.end(), [&](const Cue& c) { return c.name == n; });
            };
            f2.update(in2, g2, {}, nullptr, false, -1, 0, false, out2, cues2);
            // Eagle Eye enter & icy lightbar + exit
            g2.eagle_eye = true; in2.t = 20.1; cues2.clear(); out2 = OutputState{};
            f2.update(in2, g2, {}, nullptr, false, -1, 0, false, out2, cues2);
            CHECK(has_cue("eagle_eye_in"), "entering Eagle Eye emits calm focus cue");
            for (int i = 0; i < 15; ++i) {
                in2.t += 0.02; cues2.clear();
                f2.update(in2, g2, {}, nullptr, false, -1, 0, false, out2, cues2);
            }
            CHECK(out2.b > out2.g && out2.g > out2.r && out2.b > 120, "Eagle Eye lights lightbar icy blue-white");
            g2.eagle_eye = false; in2.t += 0.05; cues2.clear();
            f2.update(in2, g2, {}, nullptr, false, -1, 0, false, out2, cues2);
            CHECK(has_cue("eagle_eye_out"), "leaving Eagle Eye emits exit cue");

            // Fuse lightbar flicker
            EngineFrame ef_fuse; ef_fuse.fuse = 0.8f;
            for (int i = 0; i < 6; ++i) {
                in2.t += 0.02; cues2.clear();
                f2.update(in2, g2, {}, &ef_fuse, true, -1, 0, false, out2, cues2);
            }
            CHECK(out2.r > out2.g && out2.g > out2.b && out2.r > 100, "lit fuse glows warm orange-red on lightbar");

            // Progressive Gun Cleaning: gritty while dirty -> silky + gleam when clean
            g2.activity = "clean_gun"; g2.weapon_dirt = 0.85f;
            in2.t += 1.0; cues2.clear();
            f2.update(in2, g2, {}, nullptr, false, -1, 0, false, out2, cues2);
            CHECK(has_cue("cloth_rub_gritty") && has_cue("cloth_rub"),
                  "dirty gun cleaning mixes gritty scrub with cloth rub");
            g2.weapon_dirt = 0.04f;
            in2.t += 1.0; cues2.clear();
            f2.update(in2, g2, {}, nullptr, false, -1, 0, false, out2, cues2);
            CHECK(!has_cue("cloth_rub_gritty") && has_cue("cloth_rub") && has_cue("reset_click"),
                  "cleaned gun shifts to smooth wiping and polish gleam");
            g2.activity.clear();

            // Boat Rowing & Hull Waves
            g2.in_boat = true; g2.speed = 3.0f;
            in2.t += 0.1; cues2.clear();
            f2.update(in2, g2, {{WorldEvent::Swim, -0.45f, 0.8f, "oar"}}, nullptr, false, -1, 0, false, out2, cues2);
            CHECK(has_cue("oar_pull"), "boat oar stroke emits directional oar_pull cue");
            bool saw_wave = false;
            for (int i = 0; i < 40 && !saw_wave; ++i) {
                in2.t += 0.08; cues2.clear();
                f2.update(in2, g2, {}, nullptr, false, -1, 0, false, out2, cues2);
                if (has_cue("boat_wave")) saw_wave = true;
            }
            CHECK(saw_wave, "boating emits gentle hull wave lap");
            g2.in_boat = false;

            // Horse Spook & Wagon Braking
            g2.on_mount = true; g2.mount_agitation = 0.78f; g2.mount_stamina = 80; g2.mount_speed = 4.0f;
            in2.t += 0.1; cues2.clear();
            f2.update(in2, g2, {}, nullptr, false, -1, 0, false, out2, cues2);
            CHECK(has_cue("horse_spook") && has_cue("horse_heartbeat"),
                  "agitated/spooked horse emits rein tug and elevated heartbeat");
            g2.on_mount = false; g2.mount_agitation = 0.0f;

            g2.in_vehicle = true; g2.vehicle_braking = true; g2.vehicle_decel = 0.75f; g2.vehicle_surface = "gravel";
            bool saw_brake = false;
            for (int i = 0; i < 3 && !saw_brake; ++i) {
                in2.t += 0.05; cues2.clear();
                f2.update(in2, g2, {}, nullptr, false, -1, 0, false, out2, cues2);
                if (has_cue("wagon_brake")) saw_brake = true;
            }
            CHECK(saw_brake, "hard wagon braking emits tactile wheel-skid judder");
        }
    }
    printf("selftest: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

}  // namespace dsr2
