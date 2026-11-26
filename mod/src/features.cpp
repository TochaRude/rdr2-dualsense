#include "features.h"

#include "config.h"
#include "log.h"

#include <map>

namespace dsr2 {

void Features::configure() {
    Config& c = config();
    horse_gain_ = c.num("Haptics", "Horse", 0.3f);
    train_gain_ = c.num("Haptics", "Train", 0.6f);
    damage_gain_ = c.num("Haptics", "Damage", 1.0f);
    gunfire_gain_ = c.num("Haptics", "Gunfire", 0.6f);
    triggers_on_ = c.flag("Triggers", "Enabled", true);
    heart_gain_ = c.num("Haptics", "Heartbeat", 1.0f);
    cold_gain_ = c.num("Haptics", "Cold", 0.5f);
    hit_gain_ = c.num("Haptics", "HitConfirm", 0.0f);
    step_gain_ = c.num("Haptics", "Footsteps", 0.35f);
    land_gain_ = c.num("Haptics", "Landing", 0.7f);
    action_gain_ = c.num("Haptics", "Actions", 0.5f);
    minigame_gain_ = c.num("Haptics", "TableGames", 0.7f);
    weather_gain_ = c.num("Haptics", "Weather", 0.3f);
    life_gain_ = c.num("Haptics", "CampLife", 0.6f);
    confirm_gain_ = c.num("Mic", "Confirm", 1.0f);
    riding_pulse_ = c.integer("Triggers", "RidingPulse", 0);
    bool was_lb = lightbar_on_;
    lightbar_on_ = c.flag("Lightbar", "Enabled", true);
    if (was_lb && !lightbar_on_) lightbar_off_sent_ = false;
    brightness_ = c.num("Lightbar", "Brightness", 1.0f);
    ammo_leds_ = c.flag("Lightbar", "AmmoLeds", true);
    std::string lm = c.str("Lightbar", "PlayerLeds", "");
    if (lm.empty()) lm = ammo_leds_ ? "auto" : "off";      // an ini from before PlayerLeds
    for (char& ch : lm) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (lm == "off" || lm == "0") led_mode_ = LedMode::Off;
    else if (lm == "health") led_mode_ = LedMode::Health;
    else if (lm == "ammo") led_mode_ = LedMode::Ammo;
    else led_mode_ = LedMode::Auto;
    time_of_day_ = c.flag("Lightbar", "TimeOfDay", true);
}

static Cue make(const std::string& name, float gain, float pan = 0, bool has_pan = false, bool scripted = false) {
    Cue c;
    c.name = name;
    c.gain = gain;
    c.has_pan = has_pan;
    c.pan = pan;
    c.scripted = scripted;
    return c;
}

// A real step / hoof that is in water also pushes through it (deeper and faster = heavier).
void Features::through(float wade, float speed, float gain, float pan, std::vector<Cue>& cues) {
    if (wade > 0.06f) {
        float depth = clampv((wade - 0.06f) / 0.35f, 0.2f, 1.0f);
        float pace = clampv(0.55f + speed / 8.0f, 0.55f, 1.1f);
        cues.push_back(make("wade_push", gain * 1.4f * depth * pace * rng_.range(0.85f, 1.1f), pan, true));
    }
}

// Bushes (the game's own "pushing through foliage" state): a swish going in, then a rustle for as long as you keep
// walking through them - faster when running, stronger in taller bushes. Only while really walking (0.8 m/s): the
// game sets the same flag when Arthur just looks at a plant he can pick, or turns beside tall grass.
void Features::rustle(bool in, float height, float speed, float gain, bool& was, double& next, double now,
                      std::vector<Cue>& cues) {
    if (gain > 0) {
        float tall = clampv(0.5f + 0.5f * height, 0.5f, 1.0f);
        if (in && !was && speed > 0.8f) {
            cues.push_back(make("brush_in", gain * 2.0f * tall * clampv(0.6f + speed / 6.0f, 0.6f, 1.3f),
                                rng_.range(-0.3f, 0.3f), true));
            next = now + 0.18;
        } else if (in && speed > 0.8f && now >= next) {
            cues.push_back(make("brush", gain * 1.6f * tall * clampv(0.55f + speed / 6.0f, 0.75f, 1.2f) * rng_.range(0.75f, 1.1f),
                                rng_.range(-0.5f, 0.5f), true));
            next = now + clampv(0.38f - speed * 0.045f, 0.12f, 0.38f) * rng_.range(0.8f, 1.2f);
        }
    }
    was = in;
}

// Splashing into water at pace (once, when the water gets deeper than the ankles).
void Features::splash(float wade, float& was, float speed, float gain, std::vector<Cue>& cues) {
    if (wade > 0.06f && was <= 0.06f && speed > 1.5f && gain > 0)
        cues.push_back(make("splash", gain * clampv(speed / 6.0f, 0.5f, 1.4f)));
    was = wade;
}

void Features::horse(const GameSnapshot& g, double now, std::vector<Cue>& cues, OutputState& out, bool weapon_active) {
    if (!g.on_mount) {
        rearing_ = false;
        prev_agitation_ = 0.0f;
        return;
    }
    float v = g.mount_speed;
    // The hoofbeats come only from the real hoof touchdowns (World::steps -> Hoof events, in update()); there is no
    // generated gait rhythm any more. The gait table below only paces the optional riding pulse on the triggers.
    float stride_hz;
    int beats;
    if (v < 0.4f) { stride_hz = 0; beats = 0; }
    else if (v < 2.3f) { stride_hz = 0.45f + 0.25f * v; beats = 4; }     // walk, 4-beat
    else if (v < 4.6f) { stride_hz = 1.25f + 0.08f * v; beats = 2; }     // trot, 2-beat
    else if (v < 8.0f) { stride_hz = 1.5f + 0.05f * v; beats = 3; }      // canter, 3-beat
    else { stride_hz = 1.8f + 0.03f * v; beats = 4; }                    // gallop, 4-beat
    // the horse is spent: its heart pounds, it heaves
    if (g.mount_stamina < 15 && v > 2 && horse_gain_ > 0) {
        if (now >= next_horse_heart_) {
            next_horse_heart_ = now + 0.55;
            cues.push_back(make("horse_heartbeat", 0.8f * horse_gain_));
        }
        if (now >= next_breath_) {
            next_breath_ = now + 1.3;
            cues.push_back(make("horse_breath", 0.6f * horse_gain_));
        }
    }
    // rearing / bucking
    bool rear = g.mount_pitch > 25.0f;
    if (rear && !rearing_) cues.push_back(make("horse_rear", horse_gain_ * 1.2f));
    rearing_ = rear;
    // spooked / agitated horse (predators, gunfire, fear): nervous rein tug + faster heartbeat
    if (horse_gain_ > 0 && g.mount_agitation >= 0.40f) {
        float ag = clampv((g.mount_agitation - 0.35f) / 0.65f, 0.2f, 1.0f);
        if ((g.mount_agitation - prev_agitation_ >= 0.18f) || now >= next_horse_spook_) {
            cues.push_back(make("horse_spook", horse_gain_ * (0.65f + 0.65f * ag), rng_.range(-0.45f, 0.45f), true));
            next_horse_spook_ = now + clampv(1.8f - 1.1f * ag, 0.55f, 1.8f) * rng_.range(0.85f, 1.2f);
        }
        if (g.mount_agitation >= 0.60f && now >= next_horse_heart_) {
            next_horse_heart_ = now + clampv(0.70 - 0.28 * double(ag), 0.38, 0.70);
            cues.push_back(make("horse_heartbeat", horse_gain_ * (0.45f + 0.45f * ag)));
        }
    }
    prev_agitation_ = g.mount_agitation;
    // the triggers feel the gait while riding unarmed
    if (triggers_on_ && !weapon_active && riding_pulse_ > 0 && stride_hz > 0.8f && !g.aiming) {
        int hz = clampv(int(std::lround(stride_hz * float(beats) / 2)), 1, 8);
        out.r2 = fx::galloping(3, 9, 4, 7, hz);
        out.l2 = fx::galloping(3, 9, 4, 7, hz);
    }
}

void Features::transport(const GameSnapshot& g, double now, std::vector<Cue>& cues) {
    double dt = now - last_trans_t_;
    last_trans_t_ = now;
    if (dt <= 0 || dt > 0.1 || train_gain_ <= 0) return;
    if (g.in_train && g.speed > 2) {
        // 39 ft rails: a joint every ~12 m, felt twice (front and rear bogie)
        double before = rail_dist_;
        rail_dist_ += g.speed * dt;
        if (std::floor(before / 12.0) != std::floor(rail_dist_ / 12.0)) {
            float gain = train_gain_ * clampv(g.speed / 18.0f, 0.3f, 1.0f);
            cues.push_back(make("rail_clack", gain));
            // the rear bogie crosses the same joint ~10 m later
            delayed_.push_back({now + 10.0 / std::max(2.0f, g.speed), make("rail_clack", gain * 0.8f)});
        }
    } else if (g.in_vehicle && !g.in_train && !g.in_boat) {
        if (g.speed > 1.5f && now >= next_rut_) {
            next_rut_ = now + rng_.range(0.4f, 2.5f) / clampv(g.speed / 5.0f, 0.5f, 3.0f);
            cues.push_back(make("wagon_rut", train_gain_ * rng_.range(0.4f, 1.0f), rng_.range(-0.5f, 0.5f), true));
        }
        if (g.vehicle_braking && now >= next_wagon_brake_) {
            next_wagon_brake_ = now + rng_.range(0.11f, 0.18f);
            float surf_mult = (g.vehicle_surface == "gravel" || g.vehicle_surface == "stone") ? 1.15f
                            : (g.vehicle_surface == "mud" || g.vehicle_surface == "snow") ? 0.90f : 1.0f;
            Cue bc = make("wagon_brake", train_gain_ * (0.55f + 0.65f * g.vehicle_decel) * surf_mult,
                          rng_.range(-0.35f, 0.35f), true);
            if (g.vehicle_surface == "mud" || g.vehicle_surface == "snow") bc.speed = 0.85f;
            else if (g.vehicle_surface == "gravel" || g.vehicle_surface == "stone") bc.speed = 1.12f;
            cues.push_back(bc);
        }
    } else if (g.in_boat) {
        if (now >= next_boat_wave_) {
            float pace = clampv(0.6f + g.speed / 4.0f, 0.6f, 2.2f);
            next_boat_wave_ = now + rng_.range(1.4f, 2.8f) / pace;
            cues.push_back(make("boat_wave", train_gain_ * clampv(0.35f + g.speed / 10.0f, 0.35f, 0.85f) * rng_.range(0.8f, 1.1f),
                                rng_.range(-0.35f, 0.35f), true));
        }
    } else if (!g.in_train && g.near_train_dist >= 0 && g.near_train_dist < 35.0f && g.near_train_speed > 1.5f) {
        // standing near moving rail-cars / tracks: directional ground rumble + rail-joint clacks
        if (now >= next_near_train_) {
            float prox = 1.0f / (1.0f + g.near_train_dist / 8.0f);
            float spd = clampv(g.near_train_speed / 14.0f, 0.35f, 1.0f);
            float gain = train_gain_ * prox * spd;
            cues.push_back(make("train_pass", gain, g.near_train_pan, true));
            if (g.near_train_dist < 18.0f)
                cues.push_back(make("rail_clack", gain * 0.65f, g.near_train_pan, true));
            next_near_train_ = now + clampv(0.55f / std::max(0.5f, spd), 0.28f, 0.75f);
        }
    }
}

void Features::lightbar(const InputState& in, const GameSnapshot& g, const EngineFrame* w, double now, OutputState& out) {
    if (!lightbar_on_) {
        lb_[0] = lb_[1] = lb_[2] = 0;
        out.has_lightbar = true;
        out.r = out.g = out.b = 0;
        lightbar_off_sent_ = true;
        return;
    }
    lightbar_off_sent_ = false;
    float r = 0, gr = 0, b = 0;
    float t = float(now);
    if (now < damage_flash_until_) {
        r = 255 * damage_flash_; gr = 10; b = 0;
    } else if (w && w->flash) {
        r = 255; gr = 130; b = 20;
    } else if (now < honor_until_) {                                // honor changed: warm glow / dark ember
        float p = float(std::sin(kPi * (honor_until_ - now) / 1.4));
        if (honor_up_) { r = 255 * p; gr = 200 * p; b = 110 * p; }
        else { r = 120 * p; gr = 10 * p; b = 0; }
    } else if (w && w->fuse > 0.01f) {                             // lit fuse: escalating warm orange -> urgent red flicker
        float fl = 0.65f + 0.20f * std::sin(t * (18.0f + 22.0f * w->fuse)) +
                   0.15f * std::sin(t * (43.0f + 17.0f * w->fuse) + 1.7f);
        r = 255.0f * fl;
        gr = (130.0f - 85.0f * w->fuse) * fl;
        b = (25.0f - 20.0f * w->fuse) * fl;
    } else if (g.dead_eye) {
        float p = 0.7f + 0.3f * std::sin(t * 2.0f);
        r = 200 * p; gr = 120 * p; b = 30 * p;                      // sepia glow
    } else if (g.eagle_eye) {
        float p = 0.75f + 0.25f * std::sin(t * 2.4f);
        r = 120 * p; gr = 200 * p; b = 255 * p;                     // icy blue-white glow
    } else if (g.lantern) {
        float fl = 0.75f + 0.15f * std::sin(t * 13.0f) + 0.1f * std::sin(t * 31.0f + 1.3f);
        r = 255 * fl; gr = 140 * fl; b = 40 * fl;                   // warm flicker
    } else if (g.wanted && !g.halted) {
        // police / lawman alternating flash, speeding up as wanted score rises
        float wlvl = clampv(float(g.wanted_score) / 600.0f, 0.0f, 1.0f);
        double period = 0.85 - 0.45 * double(wlvl);
        bool red = std::fmod(now, period) < period * 0.5;
        r = red ? 220.0f : 30.0f; gr = 0.0f; b = red ? 0.0f : 220.0f;
    } else if (g.health_core < 25) {
        float beat = std::fmod(now, 0.9) < 0.15 ? 1.0f : 0.25f;
        r = 180 * beat;
    } else if (!in.charging() && in.battery == 0 && std::fmod(now, 4.0) < 0.25) {
        r = 160; gr = 35; b = 0;                                    // gentle low-battery blink when discharging
    } else if (time_of_day_) {
        // Smooth blend across night (5), dawn (6), day (8..17), sunset (19), night (21)
        auto lerp3 = [&](float a0, float a1, float a2, float b0, float b1, float b2, float u) {
            r = a0 + (b0 - a0) * u; gr = a1 + (b1 - a1) * u; b = a2 + (b2 - a2) * u;
        };
        float h = float(g.hour);
        if (h < 5.0f || h >= 21.0f) { r = 5; gr = 10; b = 40; }
        else if (h < 6.0f) lerp3(5, 10, 40, 120, 60, 30, h - 5.0f);
        else if (h < 8.0f) lerp3(120, 60, 30, 70, 55, 35, (h - 6.0f) * 0.5f);
        else if (h < 18.0f) { r = 70; gr = 55; b = 35; }
        else if (h < 19.0f) lerp3(70, 55, 35, 150, 60, 15, h - 18.0f);
        else lerp3(150, 60, 15, 5, 10, 40, (h - 19.0f) * 0.5f);
    }
    // dt-independent smoothing (except instant flashes)
    double dt = last_lb_t_ > 0 ? clampv(now - last_lb_t_, 0.0, 0.2) : 0.016;
    last_lb_t_ = now;
    float k = (now < damage_flash_until_ || (w && w->flash)) ? 1.0f : (1.0f - std::exp(-float(dt) / 0.04f));
    lb_[0] += (r - lb_[0]) * k;
    lb_[1] += (gr - lb_[1]) * k;
    lb_[2] += (b - lb_[2]) * k;
    out.has_lightbar = true;
    out.r = uint8_t(clampv(lb_[0] * brightness_, 0.0f, 255.0f));
    out.g = uint8_t(clampv(lb_[1] * brightness_, 0.0f, 255.0f));
    out.b = uint8_t(clampv(lb_[2] * brightness_, 0.0f, 255.0f));
}

void Features::update(const InputState& in, const GameSnapshot& g, std::vector<WorldEvent> events, const EngineFrame* weapon,
                      bool weapon_active, int clip, int clip_size, bool mic_listening, OutputState& out, std::vector<Cue>& cues) {
    double now = in.t;
    out.mute_led = mic_listening ? 0 : 1;
    if (!g.valid || g.halted) {
        delayed_.clear();
    } else {
        for (auto it = delayed_.begin(); it != delayed_.end();) {
            if (it->first <= now) { cues.push_back(it->second); it = delayed_.erase(it); }
            else ++it;
        }
    }
    if (g.valid) {
        if (prev_health_core_ < 0) {
            prev_health_core_ = g.health_core;
        } else if (std::abs(g.health_core - prev_health_core_) >= 2 || (g.health_core == 0 && prev_health_core_ != 0)) {
            prev_health_core_ = g.health_core;
            health_led_until_ = now + 3.5;
        }
    }
    if (led_mode_ == LedMode::Off) {
        out.player_leds = 0;
    } else {
        static const int masks[6] = {0x00, 0x04, 0x0A, 0x15, 0x1B, 0x1F};   // centre-out like the PS5 player LEDs
        const bool ammo_shown = led_mode_ != LedMode::Health && weapon_active && clip_size > 0 && clip >= 0;
        const bool show_health = g.valid && now < health_led_until_ &&
                                 (led_mode_ == LedMode::Health || (led_mode_ == LedMode::Auto && !ammo_shown));
        if (g.valid && g.wanted && !g.halted) {
            // police-light sweep on the 5 LEDs below the touchpad, faster at higher wanted levels
            float wlvl = clampv(float(g.wanted_score) / 600.0f, 0.0f, 1.0f);
            double period = 0.70 - 0.40 * double(wlvl);
            bool left = std::fmod(now, period) < period * 0.5;
            out.player_leds = left ? 0x03 : 0x18;                            // left 2 LEDs <-> right 2 LEDs
        } else if (ammo_shown) {
            int lit = clip == 0 ? 0 : clampv(int(std::ceil(5.0f * clip / clip_size)), 1, 5);
            out.player_leds = masks[lit];
        } else if (show_health) {
            // health indicator: 5 LEDs appear for 3.5 s whenever Arthur's health changes
            int lit = g.health_core <= 0 ? 0 : clampv(int(std::ceil(5.0f * g.health_core / 100.0f)), 1, 5);
            out.player_leds = masks[lit];
        } else {
            out.player_leds = 0;
        }
    }
    if (!g.valid) {          // standalone test: reset world state + weapon state colours only
        prev_health_core_ = -1;
        health_led_until_ = -10.0;
        weapon_seen_ = false;
        eagle_eye_was_ = false;
        wade_was_ = 0;
        horse_wade_was_ = 0;
        foliage_was_ = false;
        horse_foliage_was_ = false;
        rearing_ = false;
        prev_agitation_ = 0.0f;
        cleaning_was_ = false;
        clean_dirt_prev_ = 1.0f;
        delayed_.clear();
        lightbar(in, g, weapon, now, out);
        return;
    }
    if (!g.halted) for (auto& e : events) {
        if (e.type == WorldEvent::Damage && damage_gain_ > 0) {
            std::string name = e.cause == "melee" ? "hit_melee" : e.cause == "animal" ? "hit_animal"
                             : e.cause == "fall" ? "hit_fall" : "hit_bullet";
            cues.push_back(make(name, damage_gain_ * (0.5f + 0.5f * e.strength), e.pan, e.cause != "fall", true));
            damage_flash_until_ = now + 0.18;
            damage_flash_ = 0.5f + 0.5f * e.strength;
        } else if (e.type == WorldEvent::Gunfire && gunfire_gain_ > 0) {
            cues.push_back(make("gunfire_near", gunfire_gain_ * e.strength, e.pan, true));
        } else if (e.type == WorldEvent::Explosion && gunfire_gain_ > 0) {
            // blasts: the game's own explosion recordings; the strength by distance, the side it came from. A close one
            // (15 m or less) hits at full force and shakes the whole controller - only leaning to its side - and the
            // shock reaches the triggers
            const std::string& k = e.cause;
            std::string name = k == "thunder" ? "thunder_strike" : k == "fire" ? "explosion_fire"
                             : k == "small" ? "explosion_small" : "explosion_far";
            const bool close = e.strength >= 0.8f;
            float pan = close ? e.pan * 0.5f : e.pan;
            cues.push_back(make(name, gunfire_gain_ * 2.0f * e.strength, pan, pan != 0, true));
            if (k == "blast") cues.push_back(make("explosion_roll", gunfire_gain_ * 2.0f * e.strength, pan, pan != 0, true));
            if (close && (k == "blast" || k == "thunder")) {
                shock_t_ = now;
                shock_amp_ = e.strength;
            }
        } else if (e.type == WorldEvent::Swim) {
            // swimming: Arthur's arm pulls (left / right hand) and kicks, the horse's paddling legs, a jump into water
            if (e.cause == "paddle") {
                if (horse_gain_ > 0)
                    cues.push_back(make("horse_paddle", horse_gain_ * 1.2f * e.strength * rng_.range(0.9f, 1.08f), e.pan, true));
            } else if (e.cause == "oar") {
                if (train_gain_ > 0)
                    cues.push_back(make("oar_pull", train_gain_ * 1.15f * e.strength * rng_.range(0.9f, 1.08f), e.pan, true));
            } else if (e.cause == "dive") {
                if (step_gain_ > 0) cues.push_back(make("water_entry", std::min(1.2f, step_gain_ * 3.0f) * e.strength, 0, false, true));
            } else if (step_gain_ > 0) {
                const bool pull = e.cause == "pull";
                cues.push_back(make(pull ? "swim_pull" : "swim_kick", step_gain_ * (pull ? 1.5f : 1.0f) * e.strength * rng_.range(0.9f, 1.08f),
                                    e.pan, true));
            }
        } else if (e.type == WorldEvent::HitConfirm && hit_gain_ > 0) {
            cues.push_back(make("hit_confirm", hit_gain_, e.pan, true));
        } else if (e.type == WorldEvent::Footstep && step_gain_ > 0) {
            // boots: left foot on the left actuator, right on the right; carrying a body makes each step heavier & deeper
            Cue c = make("step_" + e.cause, step_gain_ * e.strength * rng_.range(0.9f, 1.08f), e.pan, true);
            c.speed = (g.speed < 2.2f ? 1.06f : g.speed < 4.8f ? 0.97f : 0.9f) * (g.carrying ? 0.88f : 1.0f);
            cues.push_back(c);
            through(g.wade, g.speed, step_gain_, e.pan, cues);
        } else if (e.type == WorldEvent::Hoof && horse_gain_ > 0) {
            Cue c = make("hoof_" + e.cause, horse_gain_ * e.strength * rng_.range(0.88f, 1.08f), e.pan, true);
            float v = g.mount_speed;
            c.speed = v < 2.3f ? 1.06f : 1.0f;       // a walk crisper; faster gaits carry their energy in more hoofbeats
            cues.push_back(c);
            through(g.horse_wade, v, horse_gain_ * 0.8f, e.pan, cues);
        } else if (e.type == WorldEvent::Land && land_gain_ > 0) {
            cues.push_back(make("land", land_gain_ * e.strength));
            if (step_gain_ > 0) cues.push_back(make("step_" + e.cause, step_gain_ * 1.2f));
        } else if (e.type == WorldEvent::Climb && action_gain_ > 0) {
            cues.push_back(make("climb_grab", action_gain_));
        } else if (e.type == WorldEvent::Minigame && minigame_gain_ > 0) {
            static const std::map<std::string, std::string> kCue = {
                {"knife", "knife_tap"}, {"knife_miss", "knife_finger"}, {"chips", "chips"}, {"knock", "knock"},
                {"card", "card_slide"}, {"riffle", "card_riffle"}, {"tile", "domino_clack"}};
            auto it = kCue.find(e.cause);
            if (it != kCue.end()) cues.push_back(make(it->second, minigame_gain_));
            if (e.cause == "knife_miss") {                    // the blade finds a finger
                damage_flash_ = 0.8f;
                damage_flash_until_ = now + 0.25;
            }
        } else if (e.type == WorldEvent::Life && life_gain_ > 0) {
            static const std::map<std::string, std::string> kCue = {
                {"bite", "bite"}, {"gulp", "gulp"}, {"puff", "puff"}, {"brush", "brush_stroke"}, {"feed", "horse_feed"},
                {"stow", "stow"}, {"heave", "heave"}, {"pluck", "pluck"}, {"pickup", "craft_tap"},
                {"wagon_mount", "wagon_mount"}, {"wagon_dismount", "land"}};
            auto it = kCue.find(e.cause);
            if (it != kCue.end()) {
                bool heavy = (e.cause == "wagon_mount" || e.cause == "pluck");
                float mult = e.cause == "pluck" ? 1.35f : e.cause == "wagon_mount" ? 1.25f : 1.0f;
                cues.push_back(make(it->second, life_gain_ * mult * e.strength, e.pan, e.pan != 0, heavy));
            }
        } else if (e.type == WorldEvent::Horse && (confirm_gain_ > 0 || horse_gain_ > 0)) {
            const float hgain = (e.cause == "mount" || e.cause == "dismount")
                                    ? std::max(horse_gain_ * 1.4f, action_gain_ * 1.2f)
                                    : confirm_gain_;
            const char* cue = e.cause == "rein" ? "rein_pull" : e.cause == "spur" ? "spur"
                            : e.cause == "mount" ? "horse_mount" : e.cause == "dismount" ? "land" : "horse_pat";
            if (hgain > 0) cues.push_back(make(cue, hgain * e.strength, 0.0f, false, e.cause == "mount"));
        } else if (e.type == WorldEvent::Honor) {
            honor_up_ = e.cause == "up";
            honor_until_ = now + 1.4;
            cues.push_back(make(honor_up_ ? "honor_up" : "honor_down", e.strength));
        } else if (e.type == WorldEvent::Ack && confirm_gain_ > 0) {
            cues.push_back(make(e.cause == "speech" ? "hit_confirm" : "whistle_ack",
                                e.cause == "speech" ? confirm_gain_ * 2.5f : confirm_gain_));
        } else if (e.type == WorldEvent::Door && action_gain_ > 0) {
            cues.push_back(make("door_open", action_gain_, e.pan, true));
        }
    }
    if (!g.blocked && weather_gain_ > 0 && !g.indoors) {             // rain, hail, storm wind gusts & distant thunder
        static const uint32_t kHail = joaat("HAIL"), kSleet = joaat("SLEET"), kThunder = joaat("THUNDER"),
                              kThunderstorm = joaat("THUNDERSTORM"), kHurricane = joaat("HURRICANE"),
                              kBlizzard = joaat("BLIZZARD"), kGroundBlizzard = joaat("GROUNDBLIZZARD"),
                              kWhiteout = joaat("WHITEOUT"), kSandstorm = joaat("SANDSTORM");
        const bool is_hail = (g.weather == kHail || g.weather == kSleet);
        const bool is_thunder = (g.weather == kThunder || g.weather == kThunderstorm || g.weather == kHurricane);
        const bool is_storm = is_thunder || g.weather == kBlizzard || g.weather == kGroundBlizzard ||
                              g.weather == kWhiteout || g.weather == kSandstorm;
        if ((g.rain > 0.05f || is_hail) && now >= next_drop_) {
            float intensity = std::max(g.rain, is_hail ? 0.5f : 0.0f);
            next_drop_ = now + rng_.range(0.5f, 1.5f) / (1.5f + 8.0f * intensity);
            cues.push_back(make(is_hail ? "hail_drop" : "rain_drop", weather_gain_ * rng_.range(0.35f, 1.0f),
                                rng_.range(-0.8f, 0.8f), true));
        }
        if (is_storm) {
            if (next_gust_ <= 0) next_gust_ = now + rng_.range(1.5f, 4.0f);
            else if (now >= next_gust_) {
                next_gust_ = now + rng_.range(3.5f, 8.5f);
                cues.push_back(make("wind_gust", weather_gain_ * rng_.range(0.6f, 1.1f), rng_.range(-0.6f, 0.6f), true));
            }
        } else {
            next_gust_ = 0;
        }
        if (is_thunder) {
            if (next_thunder_ <= 0) next_thunder_ = now + rng_.range(2.5f, 6.0f);
            else if (now >= next_thunder_) {
                next_thunder_ = now + rng_.range(7.0f, 16.0f);
                cues.push_back(make("thunder_roll", weather_gain_ * rng_.range(0.7f, 1.2f), rng_.range(-0.7f, 0.7f), true));
            }
        } else {
            next_thunder_ = 0;
        }
    }
    // what Arthur is busy with, felt as a texture while it lasts
    const bool is_cleaning = !g.blocked && g.activity == "clean_gun";
    if (is_cleaning && !cleaning_was_) clean_dirt_prev_ = g.weapon_dirt;
    cleaning_was_ = is_cleaning;
    if (!g.blocked && life_gain_ > 0 && !g.activity.empty() && now >= next_texture_) {
        float pan = g.activity_pan;
        std::string cue;
        double gap = 0.5;
        if (g.activity == "clean_gun") {
            float d = clampv(g.weapon_dirt, 0.0f, 1.0f);
            pan = (texture_n_ % 2) ? 0.4f : -0.1f;
            if (d > 0.15f) {
                gap = rng_.range(0.42f, 0.62f);
                cues.push_back(make("cloth_rub_gritty", life_gain_ * (0.45f + 0.75f * d) * rng_.range(0.85f, 1.1f), pan, true));
                cues.push_back(make("cloth_rub", life_gain_ * (1.0f - 0.55f * d) * rng_.range(0.8f, 1.0f), pan, true));
            } else {
                gap = rng_.range(0.38f, 0.52f);
                cues.push_back(make("cloth_rub", life_gain_ * 0.65f * rng_.range(0.85f, 1.05f), pan, true));
            }
            if (clean_dirt_prev_ >= 0.12f && d < 0.08f) {
                cues.push_back(make("reset_click", life_gain_ * 0.4f, 0.2f, true));
                clean_dirt_prev_ = d;
            } else if (d > clean_dirt_prev_) {
                clean_dirt_prev_ = d;
            }
        }
        else if (g.activity == "skin") { cue = "skin_cut"; gap = rng_.range(0.55f, 0.9f); pan = 0.3f; }
        else if (g.activity == "scrub") { cue = "scrub"; gap = rng_.range(0.35f, 0.5f); }
        else if (g.activity == "craft") { cue = "craft_tap"; gap = rng_.range(0.8f, 1.5f); pan = 0.2f; }
        else if (g.activity == "cook") { cue = "sizzle"; gap = rng_.range(0.12f, 0.3f); pan = 0.3f; }
        ++texture_n_;
        next_texture_ = now + gap;
        if (!cue.empty()) cues.push_back(make(cue, life_gain_ * rng_.range(0.75f, 1.0f), pan, true));
    }
    if (!g.halted) {                    // the world keeps moving in cutscenes: hooves, wagons and trains are felt there too
        horse(g, now, cues, out, weapon_active);
        transport(g, now, cues);
        rustle(!g.on_mount && !g.in_vehicle && !g.swimming && g.foliage, g.foliage_height, g.speed, step_gain_,
               foliage_was_, next_brush_, now, cues);
        rustle(g.on_mount && g.horse_foliage, g.horse_foliage_height, g.mount_speed, horse_gain_, horse_foliage_was_,
               next_horse_brush_, now, cues);
        if (g.on_mount) splash(g.horse_wade, horse_wade_was_, g.mount_speed, horse_gain_, cues);
        else if (!g.in_vehicle && !g.swimming) splash(g.wade, wade_was_, g.speed, step_gain_ * 1.5f, cues);
    }
    if (!g.blocked) {                   // Arthur's own state: only while you play him
        // Weapon draw / holster cues when the weapon in hand changes: sidearms (hip holster),
        // longarms (back/shoulder), and melee blades each get their own authentic speaker recording.
        if (weapon_seen_ && g.weapon != weapon_before_ && action_gain_ > 0) {
            LOGD("weapon change: 0x%08X -> 0x%08X, holstering %d", weapon_before_, g.weapon, int(g.holstering));
            auto make_carry = [&](int attach, uint32_t group, bool lantern, bool draw) {
                static const uint32_t kRifle = joaat("GROUP_RIFLE"), kRepeater = joaat("GROUP_REPEATER"),
                                      kShotgun = joaat("GROUP_SHOTGUN"), kSniper = joaat("GROUP_SNIPER"),
                                      kMelee = joaat("GROUP_MELEE");
                const char* prefix = "gun";
                bool spk = false;
                if (attach == 2 || attach == 3) {
                    prefix = "gun";
                    spk = true;
                } else if (group == kRifle || group == kRepeater || group == kShotgun || group == kSniper) {
                    prefix = "longarm";
                    spk = false;
                } else if (group == kMelee && !lantern) {
                    prefix = "blade";
                    spk = false;
                }
                Cue c = make(std::string(prefix) + (draw ? "_draw" : "_holster"), action_gain_ * 1.2f, 0.5f, true);
                c.speaker = spk;
                return c;
            };
            if (armed_before_ && attach_before_ >= 0) {
                cues.push_back(make_carry(attach_before_, group_before_, lantern_before_, false));
            }
            if (g.armed && g.attach_point >= 0) {
                delayed_.push_back({now + (armed_before_ ? 0.25 : 0.0),
                                    make_carry(g.attach_point, g.group, g.lantern, true)});
            }
        }
        if (!weapon_seen_) {
            dead_eye_was_ = g.dead_eye;
            eagle_eye_was_ = g.eagle_eye;
        } else {
            if (g.dead_eye != dead_eye_was_) {
                dead_eye_was_ = g.dead_eye;
                if (g.dead_eye) cues.push_back(make("deadeye_in", 0.65f, 0, false, true));
                else cues.push_back(make("deadeye_out", 0.50f, 0, false, false));
            }
            if (g.eagle_eye != eagle_eye_was_) {
                eagle_eye_was_ = g.eagle_eye;
                if (g.eagle_eye) cues.push_back(make("eagle_eye_in", 0.55f, 0, false, false));
                else cues.push_back(make("eagle_eye_out", 0.40f, 0, false, false));
            }
        }
        weapon_seen_ = true;
        armed_before_ = g.armed;
        group_before_ = g.group;
        lantern_before_ = g.lantern;
        attach_before_ = g.attach_point;
        weapon_before_ = g.weapon;
        if (heart_gain_ > 0 && !g.dead_eye && now >= next_heart_) {
            if (g.health_core < 20) {
                next_heart_ = now + 0.9;
                cues.push_back(make("heartbeat", heart_gain_ * 0.9f));
            } else if (g.wanted) {
                // wanted tension heartbeat: faster and stronger as wanted level increases
                float wlvl = clampv(float(g.wanted_score) / 600.0f, 0.0f, 1.0f);
                next_heart_ = now + (1.10 - 0.55 * wlvl);
                cues.push_back(make("heartbeat", heart_gain_ * (0.45f + 0.45f * wlvl)));
            }
        }
        if (cold_gain_ > 0 && g.temperature < -5 && now >= next_shiver_) {
            next_shiver_ = now + rng_.range(4.0f, 9.0f);
            cues.push_back(make("shiver", cold_gain_ * clampv((-g.temperature - 5) / 10.0f + 0.4f, 0.4f, 1.0f)));
        }
        // carrying a body / heavy load: periodic weight shift while walking + subtle grip resistance
        if (g.carrying && !g.on_mount && !g.in_vehicle) {
            if (life_gain_ > 0 && g.speed > 0.6f && now >= next_carry_) {
                next_carry_ = now + rng_.range(0.85f, 1.4f);
                cues.push_back(make("carry_shift", life_gain_ * clampv(g.speed / 3.0f, 0.5f, 1.0f), 0.35f, true));
            }
            if (triggers_on_ && !g.armed) {
                out.l2 = fx::feedback(4, 2);
                out.r2 = fx::feedback(4, 2);
            }
        }
    }
    // a close blast's shock in both triggers (~0.45 s) when no active weapon/menu effect holds them
    double since = now - shock_t_;
    if (triggers_on_ && !g.halted && !g.blocked && !weapon_active && since >= 0 && since < 0.45) {
        int a = int(std::lround(shock_amp_ * (since < 0.15 ? 8 : since < 0.3 ? 5 : 2)));
        if (a > 0) out.l2 = out.r2 = fx::vibration(0, a, 35);
    }
    lightbar(in, g, weapon, now, out);
}

}  // namespace dsr2
