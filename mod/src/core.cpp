#include "core.h"

#include "game_files.h"

#include "config.h"
#include "dialogue.h"
#include "gamemem.h"
#include "input.h"
#include "log.h"
#include "text_inject.h"
#include "world.h"

namespace dsr2 {

Core& core() {
    static Core* c = new Core();
    return *c;
}

void Core::load_data() {
    Config& c = config();
    c.load((module_dir() / "DualSenseRDR2.ini").wstring());
    log::set_debug(c.flag("General", "Debug", false));
    reload_key_ = c.integer("General", "ReloadKey", 120);
    wheel_click_gain_ = c.num("Haptics", "WheelClick", 0.6f);
    triggers_on_ = c.flag("Triggers", "Enabled", true);
    trigger_strength_ = clampv(c.num("Triggers", "Strength", 1.0f), 0.0f, 2.0f);
    haptics_on_ = c.flag("Haptics", "Enabled", true);
    record_cues_ = log::debug_enabled() && c.flag("General", "Record", false);
    const bool spk_on = c.flag("Speaker", "Mechanics", false) && haptics_on_;
    speaker_mechanics_ = c.flag("Speaker", "Mechanics", false);
    speaker_ambient_ = c.flag("Speaker", "Ambient", true);
    speaker_ui_ = c.flag("Speaker", "UI", true);
    const float spk_vol = spk_on ? c.num("Speaker", "Volume", 0.35f) : 0.0f;
    LOGI("speaker: %s (volume %.2f)", spk_on ? "on" : "off", spk_vol);
    settings_ = Settings{};
    settings_.manual_action = c.flag("Triggers", "ManualCocking", true);
    settings_.hammer_slip = c.flag("Triggers", "HammerSlip", true) && c.flag("Triggers", "ConditionGrit", true);
    settings_.fanning = c.flag("Triggers", "Fanning", true);
    settings_.auto_align = c.flag("Triggers", "AutoAlign", true);
    settings_.fire_margin = c.integer("Triggers", "FireMargin", 4);
    settings_.kick_scale = c.num("Triggers", "KickStrength", 1.0f);
    settings_.aim_resistance = clampv(c.integer("Triggers", "AimResistance", 1), 0, 2);
    settings_.reload_ticks = !in_game_;
    settings_.scheduled_explosions = !in_game_;   // in game the real explosion is felt (World: IS_EXPLOSION_IN_SPHERE)
    cal_ = Calibration{};
    if (cal_.load(data_dir() / "calibration.json")) LOGI("calibration loaded");
    else LOGI("no calibration.json - estimated trigger zones (auto-align learns where your triggers break)");
    catalog_.load(data_dir() / "weapons.json");
    fs::path game_dir = module_dir();
    std::string override_dir = c.str("General", "GameDir", "");       // test harness: point at the game
    if (!override_dir.empty()) game_dir = fs::path(override_dir);
    game_audio_.discover(game_dir, data_dir() / "sound_labels.json");
    lib_.load(data_dir() / "haptics.json", c.flag("Haptics", "GameShots", true) ? &game_audio_ : nullptr);
    features_.configure();
    input().configure();
    audio_.set_strength(c.num("Haptics", "Strength", 1.0f));
    audio_.set_speaker_volume(spk_vol);
    audio_.set_duck(c.num("Haptics", "Duck", 0.3f));
    game_rumble_gain_ = c.num("Haptics", "GameRumble", 0.15f);
    audio_.set_rumble_gain(frozen_rumble_fallback_ ? std::max(game_rumble_gain_, 0.6f) : game_rumble_gain_);
    // Bluetooth: haptics as audio in the controller's reports (Bluetooth=0: the rumble motors instead). The sample
    // format and the channel order are switchable while testing (BluetoothFormat=s8 / u8, BluetoothSwap=1).
    audio_.set_bluetooth_options(c.flag("Haptics", "Bluetooth", true), c.str("Haptics", "BluetoothFormat", "s8") == "u8",
                                 c.flag("Haptics", "BluetoothSwap", false));
    audio_.set_game_audio_gain(c.num("Haptics", "GameAudio", 0.0f));
    dialogue().configure();
    engine_.reset();
    engine_key_ = 0;
}

bool Core::init(bool in_game, const std::string& test_weapon) {
    shutting_down_ = false;
    in_game_ = in_game;
    test_weapon_ = test_weapon;
    load_data();
    Config& c = config();
    if (!c.flag("General", "Enabled", true)) {
        enabled_ = false;
        initialized_ = true;
        LOGI("disabled in DualSenseRDR2.ini");
        return false;
    }
    enabled_ = true;
    if (in_game) input().install_hook();
    audio_.set_bluetooth_output([this](const uint8_t* pcm) { return pad_.send_haptics(pcm); },
                                [this] { return pad_.connected() && !pad_.usb(); });
    audio_.start(&lib_);
    init_t_ = now_s();
    title_cross_armed_ = true;
    if (in_game) dialogue().start(module_dir(), &audio_);      // cutscene / dialogue haptics (idle while both are off)
    audio_.start_game_audio(c.num("Haptics", "GameAudio", 0.0f), c.str("Haptics", "GameAudioDevice", ""));
    apply_mic();
    pad_.start([this](const InputState& s, OutputState& out) { return on_input(s, out); });
    initialized_ = true;
    LOGI("started (%s)", in_game ? "in game" : "standalone test");
    return true;
}

// Whistle and voice commands from the ini - at start and after every reload (settings page, F9), so switching
// them on / off or changing the phrases works without restarting the game.
void Core::apply_mic() {
    Config& c = config();
    whistle_on_ = c.flag("Mic", "WhistleForHorse", false);
    if (whistle_on_)
        audio_.start_mic(c.num("Mic", "Sensitivity", 1.0f), [this] { whistle_at_ = now_s(); });   // acted on in on_input
    else
        audio_.stop_mic();
    // each command may list several spellings/words, comma separated: chao = ciao / chow
    auto words = [&](const char* key, const char* def) {
        std::vector<std::string> out;
        std::string cur;
        for (char ch : c.str("Voice", key, def) + ",") {
            if (ch == ',') {
                while (!cur.empty() && cur.back() == ' ') cur.pop_back();
                if (!cur.empty()) out.push_back(cur);
                cur.clear();
            } else if (ch != ' ' || !cur.empty()) {
                cur += char(tolower((unsigned char)ch));
            }
        }
        return out;
    };
    bool on = c.flag("Voice", "Enabled", false);
    std::vector<std::string> greet = words("Phrase", "ciao,chow"), calm = words("CalmPhrase", "easy,easy boy,easy girl,steady"),
                             stop = words("StopPhrase", ""), spur = words("SpeedPhrase", "");
    std::vector<std::string> phrases = greet;
    phrases.insert(phrases.end(), calm.begin(), calm.end());
    phrases.insert(phrases.end(), stop.begin(), stop.end());
    phrases.insert(phrases.end(), spur.begin(), spur.end());
    float min_conf = c.num("Voice", "MinConfidence", 0.7f);
    std::string key = on ? std::to_string(min_conf) : "off";
    for (auto& p : phrases) key += "|" + p;
    if (key == voice_key_) return;                  // nothing changed: leave the recognizer running
    voice_key_ = key;
    voice_.stop();
    voice_greet_ = greet, voice_calm_ = calm, voice_stop_ = stop, voice_spur_ = spur;
    if (!on || phrases.empty()) {
        LOGI("voice commands off");
        return;
    }
    voice_.start(phrases, min_conf, [this](const std::string& said, float conf) {
        last_command_t_ = now_s();
        LOGI("voice: \"%s\" (confidence %.2f)", said.c_str(), conf);
        auto in = [&](const std::vector<std::string>& v) { return std::find(v.begin(), v.end(), said) != v.end(); };
        if (in_game_) {
            if (in(voice_greet_)) world().request_greet();
            else if (in(voice_calm_)) world().request_horse(World::HorseCalm);
            else if (in(voice_stop_)) world().request_horse(World::HorseStop);
            else if (in(voice_spur_)) world().request_horse(World::HorseSpur);
        } else {                                 // test program: just confirm
            confirm();
            printf("\n  >>> %s\n", in(voice_greet_) ? "CHAO" : in(voice_calm_) ? "EASY (calm the horse)" : in(voice_stop_) ? "WHOA (stop the horse)" : "FASTER (spur the horse)");
        }
    });
}

void Core::load_game_files() {
    game_files::preload(game_audio_.game_files());
    request_reload();                                // rebuilds the effects that use recordings
}

// "heard you": two firm thumps and a short lightbar flash
void Core::confirm() {
    if (!haptics_on_) return;
    Cue ack;
    ack.name = "whistle_ack";
    ack.gain = config().num("Mic", "Confirm", 1.0f);
    ack.scripted = false;
    if (ack.gain > 0) audio_.play(ack);
    audio_.ack_flash_until = now_s() + 0.35;
}

void Core::shutdown() {
    shutting_down_ = true;
    {
        std::lock_guard<std::mutex> l(reconnect_mu_);
        if (reconnect_thread_.joinable()) reconnect_thread_.join();
    }
    pad_.stop();
    voice_.stop();
    dialogue().stop();
    audio_.stop();
    world().shutdown();
    initialized_ = false;
    enabled_ = false;
    LOGI("stopped");
}

void Core::build_engine(const nlohmann::json& prof0, const std::string& name) {
    if (engine_) learned_break_[engine_->id()] = engine_->break_raw();
    engine_.reset();
    {
        std::lock_guard<std::mutex> l(status_mu_);
        engine_name_ = name;
    }
    if (prof0.is_null()) return;
    nlohmann::json prof = prof0;
    std::string shot = lib_.ensure_weapon(prof);
    std::vector<std::vector<int>> kicks;
    if (!shot.empty()) {
        prof["recoil"]["haptic"] = shot;
        kicks = lib_.kicks(shot);
    }
    auto lb = learned_break_.find(prof.value("id", std::string()));
    if (lb != learned_break_.end()) prof["fire"]["break_raw"] = lb->second;
    Settings s = settings_;
    s.audio_latency_ms = std::min(25.0f, audio_.latency_ms());
    try {
        WeaponEngine::learned_path = in_game_ ? data_dir() / "learned.json" : fs::path();
        engine_ = std::make_unique<WeaponEngine>(prof, cal_, s, kicks);
        LOGI("weapon: %s (%s, fires at raw %d)", name.c_str(), engine_->kind().c_str(), engine_->break_raw());
    } catch (const std::exception& e) {
        LOGE("profile %s: %s", name.c_str(), e.what());
    }
}

bool Core::on_input(const InputState& s, OutputState& out) {
    if (reload_requested_.exchange(false)) {
        load_data();
        apply_mic();
        // the game-audio layer starts once it is above 0 - also when switched on in the settings page
        audio_.start_game_audio(config().num("Haptics", "GameAudio", 0.0f), config().str("Haptics", "GameAudioDevice", ""));
        LOGI("settings and data reloaded");
        audio_.ack_flash_until = now_s() + 0.4;   // the confirmation flash: after the reload (it takes ~0.6 s)
    }
    GameSnapshot g = in_game_ ? world().snapshot() : GameSnapshot{};
    std::vector<WorldEvent> events = in_game_ ? world().take_events() : std::vector<WorldEvent>{};
    // In the pause menu and on loading screens the game's scripts stand still - and so does our picture of the game.
    // A stale picture must not keep anything going (the gait rhythm galloped on in the pause menu): treat it as
    // halted, mute the game-clock tracks (dialogue) and the game's rumble until it moves again.
    const bool frozen = in_game_ && g.time > 0.0 && s.t - g.time > 0.25;
    const double frozen_for = frozen ? (s.t - g.time) : 0.0;
    const bool in_menu = in_game_ && (!g.valid || g.menu || frozen);
    if (g.valid) ever_valid_ = true;
    const float rl = input().rumble_low(), rh = input().rumble_high();
    if (frozen) {
        g.halted = true;
        g.blocked = true;
    }
    audio_.set_tracks_muted(frozen || in_menu);

    // a whistle is acted on at once - unless the speech recognizer has just heard / guessed a command ("chao")
    double wt = whistle_at_.exchange(-1);
    if (wt > 0) {
        double ct = std::max(last_command_t_.load(), voice_.last_command_guess.load());
        bool playing = !in_game_ || (g.valid && !g.blocked && !g.menu && !g.wheel);
        if (!whistle_on_) {
            // switched off
        } else if (ct > wt - 1.0 && ct <= wt + 0.05) {
            LOGI("whistle ignored: it was a spoken word");
        } else if (!playing) {
            LOGI("whistle ignored: a menu is open / not playing");
        } else if (in_game_ && s.l2 > 30) {
            // the whistle is D-pad Up - aiming it raises the gun, focused on someone it greets them
            LOGI("whistle ignored: L2 held (aiming / focused on someone)");
        } else {
            input().inject(XINPUT_GAMEPAD_DPAD_UP, 0.25);
            confirm();
            if (!in_game_) printf("\n  >>> WHISTLE\n");
        }
    }

    // which weapon is in hand (when unarmed on a horse or in a vehicle, R1/Circle are horse brake/calm - never run unarmed fist melee!)
    uint32_t key = 0;
    if (in_game_) {
        if (frozen) {
            key = engine_key_;             // paused: the same gun (not rebuilt), just inactive
        } else if (g.valid && !g.blocked && g.weapon) {
            const bool riding_unarmed = !g.armed && (g.on_mount || g.in_vehicle || g.in_train || g.in_boat);
            if (!riding_unarmed) key = g.weapon;
        }
    } else if (!test_weapon_.empty()) {
        key = joaat(test_weapon_);
    }
    if (key != engine_key_) {
        engine_key_ = key;
        if (!key) build_engine(nlohmann::json(), "");
        else if (in_game_) {
            std::string name;
            nlohmann::json p = catalog_.resolve(key, g.group, name);
            if (p.is_null()) LOGI("weapon 0x%08X (group 0x%08X) not in the catalog - no trigger effects", key, g.group);
            build_engine(p, name.empty() ? "unknown" : name);
        } else {
            build_engine(catalog_.resolve_name(test_weapon_), test_weapon_);
        }
    }

    GameHints h;
    if (in_game_ && g.valid) {
        h.valid = true;
        h.clip = g.clip;
        h.clip_max = g.clip_max;
        h.reloading = g.reloading;
        h.shooting = g.shooting;
        h.dead_eye = g.dead_eye;
        h.grit = g.grit;
        h.kick_mult = g.kick_mult;
        h.tremor = g.tremor;
        h.lasso_attached = g.lasso_attached;
        h.lasso_pull = g.lasso_pull;
        h.melee_hits = g.melee_hits;
        h.melee_ko = g.melee_ko;
        h.melee_target_near = g.melee_target_near;
        h.can_quickdraw = g.can_quickdraw;
        h.hip_fire = !g.aiming;
        h.dual_wield = g.dual_wield;
        h.ready = g.ready;
        h.fishing = g.fishing;
        h.deadeye_marks = g.deadeye_marks;
    }
    // weapon / item wheel: L2/R2 switch weapons there - not shots. Pause the trigger engine while it is
    // open (L1 held, or the game says so) and until R2 has been let go afterwards, never in menus/loading.
    const bool allow_wheel = !in_menu && (!in_game_ || !g.blocked || g.wheel);
    if (allow_wheel && s.pressed(L1)) { if (l1_since_ < 0) l1_since_ = s.t; } else l1_since_ = -1;
    bool wheel = allow_wheel && ((l1_since_ >= 0 && s.t - l1_since_ > 0.12) || (in_game_ && g.wheel));
    if (wheel) { wheel_until_ = s.t + 0.25; need_r2_release_ = true; }
    else if (!allow_wheel) { wheel_until_ = 0; need_r2_release_ = false; }
    else if (wheel_open_ && in_game_ && !g.wheel) { wheel_until_ = 0; }      // game closed wheel: weapon ready immediately
    if (need_r2_release_ && s.t > wheel_until_ && s.r2 < 25) need_r2_release_ = false;
    bool paused = s.t < wheel_until_ || need_r2_release_;
    EngineFrame frame;
    const uint32_t pressed = s.buttons & ~prev_buttons_;
    prev_buttons_ = s.buttons;
    bool can_run_engine = engine_ && !paused && !frozen && !(in_game_ && (g.blocked || g.swimming));
    bool weapon_active = can_run_engine && triggers_on_;
    if (can_run_engine) frame = engine_->update(s, h);
    if (in_game_ && frame.flash) world().notify_shot(s.t);
    std::vector<Cue> cues = std::move(frame.cues);
    if (weapon_active) {
        out.r2 = frame.r2;
        out.l2 = frame.l2;
    }
    features_.update(s, g, std::move(events), engine_ ? &frame : nullptr, weapon_active,
                     engine_ ? engine_->clip() : -1, engine_ ? engine_->clip_size() : 0, audio_.mic_listening(), out, cues);
    // Keep adaptive triggers relaxed and suppress haptics in main menus, pause menus, and loading screens.
    if (in_menu) {
        out.r2 = fx::off();
        out.l2 = fx::off();
        cues.clear();
        const bool texts_ready = text_inject_ready();
        const bool nav_pressed = (pressed & (DPAD_UP | DPAD_DOWN | DPAD_LEFT | DPAD_RIGHT | L1 | R1 | CIRCLE)) != 0;
        const bool intro_done = (s.t - init_t_ >= 12.0) || (texts_ready && nav_pressed);
        if (!ever_valid_ && g.time == 0.0 && texts_ready && intro_done && nav_pressed)
            title_cross_armed_ = true;
        // Confirm thump when pressing Cross on the loaded Main Menu (e.g. Story) or in-game Pause Menu
        if (allow_ui_confirm(ever_valid_, g.time, g.menu, texts_ready, intro_done, title_cross_armed_) &&
            (pressed & CROSS) && s.t - last_ui_confirm_t_ >= 0.18 && wheel_click_gain_.load() > 0) {
            last_ui_confirm_t_ = s.t;
            if (!ever_valid_) title_cross_armed_ = false;
            Cue c;
            c.name = "ui_confirm";
            c.gain = std::max(0.55f, wheel_click_gain_.load());
            c.scripted = false;
            cues.push_back(c);
        }
    }
    // in the wheel L2/R2 browse weapons: each press is a light selector click (a detent that gives way),
    // with a small tick - a switch, not a shot. After closing it R2 stays loose until let go.
    if (!in_menu && s.t < wheel_until_ && triggers_on_) {
        out.r2 = fx::weapon(2, 4, 3);
        out.l2 = fx::weapon(2, 4, 3);
        int click_r = cal_.raw_at(4, true), click_l = cal_.raw_at(4, false);
        Cue tick;
        tick.name = "wheel_tick";
        tick.gain = wheel_click_gain_.load();
        tick.has_pan = true;
        if (tick.gain > 0 && s.r2 >= click_r && wheel_r2_ < click_r) { tick.pan = 0.6f; cues.push_back(tick); }
        if (tick.gain > 0 && s.l2 >= click_l && wheel_l2_ < click_l) { tick.pan = -0.6f; cues.push_back(tick); }
        // switching wheel tabs (Weapons / Items / Horse) with R1
        if (tick.gain > 0 && g.wheel && (pressed & R1)) {
            tick.pan = 0.35f; cues.push_back(tick);
            LOGD("wheel: R1 tab switch");
        }
        // moving the highlight to another weapon / item (the game reports which one is highlighted)
        if (tick.gain > 0 && g.wheel && wheel_open_ && g.wheel_item != wheel_item_) {   // also onto / off empty slots
            tick.pan = 0; cues.push_back(tick);
            LOGD("wheel: highlight %08X", g.wheel_item);
        }
    }
    wheel_item_ = in_game_ ? g.wheel_item : 0;
    wheel_open_ = in_game_ && g.wheel && !in_menu;
    wheel_r2_ = s.r2;
    wheel_l2_ = s.l2;
    if (!in_menu && triggers_on_) {
        out.r2 = fx::scale(out.r2, trigger_strength_);
        out.l2 = fx::scale(out.l2, trigger_strength_);
    }
    if (!g.valid && !in_game_ && engine_) {        // standalone: engine state on the lightbar
        static const std::map<std::string, std::array<uint8_t, 3>> colors = {
            {"ready", {0, 40, 0}}, {"held", {40, 25, 0}}, {"action", {0, 0, 70}}, {"cycling", {0, 30, 80}},
            {"reloading", {40, 0, 40}}, {"draw", {60, 50, 0}}, {"firing", {255, 60, 0}}, {"tethered", {70, 35, 0}}};
        auto it = colors.find(frame.state);
        if (frame.flash) { out.r = 255; out.g = 120; out.b = 0; }
        else if (it != colors.end()) { out.r = it->second[0]; out.g = it->second[1]; out.b = it->second[2]; }
        out.has_lightbar = true;
    }
    if (s.t < audio_.ack_flash_until.load()) {          // recognition flash
        out.has_lightbar = true;
        out.r = 40; out.g = 160; out.b = 255;
    }
    // Route controller speaker output by category when "Sounds from the Controller" is active.
    const bool spk_on = speaker_mechanics_.load() && haptics_on_;
    out.speaker_enabled = spk_on;
    for (auto& c : cues) {
        if (!spk_on) {
            c.speaker = false;
            continue;
        }
        switch (HapticLibrary::speaker_category(c.name)) {
        case SpeakerCategory::Mech:
            if (c.name != "gun_draw" && c.name != "gun_holster" && c.name != "bow_creak") c.speaker = true;
            break;
        case SpeakerCategory::Ambient: c.speaker = speaker_ambient_.load(); break;
        case SpeakerCategory::UI: c.speaker = speaker_ui_.load(); break;
        case SpeakerCategory::None: c.speaker = false; break;
        }
    }
    // Contrast (Returnal: the heaviest feel is kept for the greatest threats): while shots fly and hits land - ours,
    // others' or an explosion - the everyday textures (steps, hooves, wading, bushes, rain, wheels) step back
    bool fight = frame.flash;
    for (const Cue& c : cues)
        if (c.name == "gunfire_near" || c.name.rfind("hit_", 0) == 0 || c.name.rfind("explosion", 0) == 0) fight = true;
    if (fight) fight_until_ = s.t + 3.0;
    if (s.t < fight_until_)
        for (Cue& c : cues) {
            const std::string& n = c.name;
            if (n.rfind("step_", 0) == 0 || n.rfind("hoof_", 0) == 0 || n == "wade_push" || n == "brush" || n == "brush_in" || n == "splash" ||
                n == "rain_drop" || n == "wagon_rut" || n == "wagon_brake" || n == "rail_clack" || n == "swim_pull" || n == "swim_kick" ||
                n == "horse_paddle" || n == "oar_pull" || n == "boat_wave")
                c.gain *= 0.5f;
        }
    if (record_cues_ && haptics_on_)            // research (Record=1): every effect as it plays
        for (const auto& c : cues) LOGD("cue: %s %.2f%s", c.name.c_str(), c.gain, c.has_pan ? (c.pan < -0.2f ? " left" : c.pan > 0.2f ? " right" : "") : "");
    // rumble only (Bluetooth without haptics): the fine textures would blur into one endless rumble - left out
    const bool rumble_only = !audio_.ok();
    if (haptics_on_)
        for (auto& c : cues) {
            if (rumble_only && (c.name == "rain_drop" || c.name == "brush" || c.name == "brush_in" || c.name == "wade_push"))
                continue;
            if (in_game_ && g.cutscene) LOGD("cutscene: felt %s (%.2f)", c.name.c_str(), c.gain);   // which world events register in cutscenes
            audio_.play(c);
        }
    // debug: what still plays while the game state is frozen (should be nothing)
    if (in_game_ && g.time > 0.0 && log::debug_enabled()) {
        if (frozen != frozen_) {
            frozen_ = frozen;
            frozen_log_t_ = s.t;
            frozen_cues_.clear();
            if (frozen) LOGD("game state frozen (pause menu / loading) - effects paused");
            else LOGD("game state live again");
        }
        if (frozen) {
            if (haptics_on_)
                for (const Cue& c : cues) frozen_cues_[c.name]++;
            if (s.t - frozen_log_t_ >= 1.0) {
                std::string what;
                for (const auto& [name, n] : frozen_cues_) what += " " + name + " x" + std::to_string(n);
                LOGD("frozen %.0f s: cues%s | game rumble %.2f / %.2f | haptic tracks %d", s.t - g.time,
                     what.empty() ? " none" : what.c_str(), input().rumble_low(), input().rumble_high(),
                     audio_.track_count());
                frozen_cues_.clear();
                frozen_log_t_ = s.t;
            }
        }
    }

    int rt = weapon_active ? frame.game_rt : -1;
    input().publish(s, rt, true);
    // a controller the game has not seen (plugged in after the game started, or while our controller was not open
    // yet): mark slot 1 as the game's own pad check would have - from now on the game reads it every frame
    if (in_game_ && s.t >= next_slot_check_) {
        next_slot_check_ = s.t + 0.5;
        if (gamemem::pad_slot_marked(0) == 0) {
            bool ok = gamemem::mark_pad_slot(0);
            LOGI("controller: the game had not seen it - %s", ok ? "marked its pad slot" : "could NOT mark its pad slot");
        }
    }
    if (s.fresh) {
        input().track_touch(s);
        const bool real_weapon_aim = in_game_ ? (g.valid && g.armed && g.free_aim && !g.lock_on && !g.blocked && !paused)
                                              : (s.l2 > 128);
        const bool can_gyro = !in_game_ || (g.valid && !g.blocked && !paused && !in_menu);
        input().track_gyro(s, real_weapon_aim, can_gyro);
    }
    if (frozen) {
        if (frozen_for >= 15.0 && !frozen_warned_) {
            frozen_warned_ = true;
            LOGW("game state frozen for %.0f s - script thread is not ticking (pause/loading or ScriptHook stopped)", frozen_for);
        }
        const bool fallback = should_fallback_rumble(frozen, paused, frozen_for, rl, rh, frozen_rumble_fallback_);
        if (fallback && !frozen_rumble_fallback_) {
            frozen_rumble_fallback_ = true;
            audio_.set_rumble_gain(std::max(game_rumble_gain_, 0.6f));
            LOGW("script thread stalled for %.0f s while game is active - falling back to native game rumble", frozen_for);
        }
        if (fallback) audio_.set_rumble(rl, rh);
        else audio_.set_rumble(0, 0);
    } else {
        if (frozen_rumble_fallback_) {
            frozen_rumble_fallback_ = false;
            audio_.set_rumble_gain(game_rumble_gain_);
            LOGI("script thread live again - full DualSense haptics restored");
        }
        frozen_warned_ = false;
        if (paused || in_menu) audio_.set_rumble(0, 0);   // mute game rumble in wheel/menus/loading
        else audio_.set_rumble(rl, rh);
    }
    if (!audio_.ok() && haptics_on_) {
        out.has_rumble = true;
        audio_.rumble_tick(s.t, out.rumble_left, out.rumble_right);
        if (log::debug_enabled()) {                    // debug: what the motors are actually given (Bluetooth)
            bt_max_l_ = std::max(bt_max_l_, int(out.rumble_left));
            bt_max_r_ = std::max(bt_max_r_, int(out.rumble_right));
            if (s.t - bt_log_t_ > 2.0) {
                LOGD("bluetooth rumble: strongest in 2 s left %d right %d (of 255), game rumble %.2f / %.2f", bt_max_l_,
                     bt_max_r_, input().rumble_low(), input().rumble_high());
                bt_max_l_ = bt_max_r_ = 0;
                bt_log_t_ = s.t;
            }
        }
    }
    // hold PS ~1 s: reload the ini and the data, like F9 (the game never sees the PS button)
    if (s.buttons & PS) {
        if (ps_since_ < 0) ps_since_ = s.t;
        else if (!ps_fired_ && s.t - ps_since_ >= 1.0) {
            ps_fired_ = true;
            LOGI("PS held: reloading settings and data");
            request_reload();
        }
    } else {
        ps_since_ = -1;
        ps_fired_ = false;
    }
    if (pressed & MUTE) {
        audio_.set_mic_listening(!audio_.mic_listening());
        voice_.set_listening(audio_.mic_listening());
        LOGI("whistle + voice listening %s", audio_.mic_listening() ? "on" : "off");
    }
    {
        std::lock_guard<std::mutex> l(status_mu_);
        last_state_ = frame.state;
        last_rt_ = rt >= 0 ? rt : s.r2;
    }
    return true;
}

void Core::script_tick() {
    if (!initialized_.load() || !enabled_.load()) return;
    if (in_game_) {
        // perf probe (debug log only): game FPS and how long our per-frame work takes, once a second
        double t0 = now_s();
        world().tick();
        if (log::debug_enabled()) {
            double ms = (now_s() - t0) * 1000;
            perf_sum_ += ms;
            perf_max_ = std::max(perf_max_, ms);
            ++perf_n_;
            float ft = world().frame_time() * 1000.0f;
            perf_frame_max_ = std::max(perf_frame_max_, double(ft));
            if (ms > 50) LOGD("perf: our frame work took %.0f ms", ms);
            if (t0 - perf_t_ >= 1.0) {
                LOGD("perf: %d frames/s (slowest %.0f ms), our work avg %.2f ms, max %.1f ms", perf_n_, perf_frame_max_,
                     perf_sum_ / std::max(1, perf_n_), perf_max_);
                perf_t_ = t0;
                perf_sum_ = perf_max_ = perf_frame_max_ = 0;
                perf_n_ = 0;
            }
        }
    }
    if (!pad_.connected()) input().publish(InputState{}, -1, false);   // let other pads through
    // the controller came back (cable, Bluetooth, sleep): Windows recreated its audio devices
    bool conn = pad_.connected();
    if (conn && !pad_was_connected_ && (pad_seen_ || !audio_.ok()) && !shutting_down_.load() && !restarting_.exchange(true)) {
        LOGI("controller %s - reopening its audio (haptics, mic, voice)", pad_seen_ ? "reconnected" : "connected after the game started");
        std::lock_guard<std::mutex> l(reconnect_mu_);
        if (reconnect_thread_.joinable()) reconnect_thread_.join();
        reconnect_thread_ = std::thread([this] {
            for (int i = 0; i < 30 && !shutting_down_.load(); ++i) Sleep(50);
            if (!shutting_down_.load()) {
                audio_.restart();
                voice_.restart();
            }
            restarting_ = false;
        });
    }
    if (conn) pad_seen_ = true;
    pad_was_connected_ = conn;
    int key = reload_key_.load();
    bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
    if (down && !reload_key_down_) request_reload();
    reload_key_down_ = down;
}

std::string Core::status() {
    std::lock_guard<std::mutex> l(status_mu_);
    char buf[256];
    snprintf(buf, sizeof buf, "pad %s %.0f Hz | audio %s %.1f ms | weapon %s | state %s | RT %d",
             pad_.connected() ? pad_.name().c_str() : "-", pad_.report_rate(), audio_.ok() ? "ok" : "off",
             audio_.latency_ms(), engine_name_.c_str(), last_state_.c_str(), last_rt_);
    return buf;
}

}  // namespace dsr2
