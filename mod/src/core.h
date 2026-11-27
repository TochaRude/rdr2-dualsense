// Owns every subsystem and runs the controller pipeline.
#pragma once

#include "audio.h"
#include "catalog.h"
#include "common.h"
#include "ds5.h"
#include "engine.h"
#include "features.h"
#include "gameaudio.h"
#include "haptics.h"
#include "voice.h"

#include <map>
#include <memory>

namespace dsr2 {

class Core {
public:
    ~Core() {
        if (initialized_.load() || reconnect_thread_.joinable()) shutdown();
    }
    bool init(bool in_game, const std::string& test_weapon = "");
    void shutdown();
    void script_tick();                 // ScriptHook thread (in game) or test loop
    void request_reload() { reload_requested_ = true; }
    void request_exit_reset() { exit_reset_pending_.store(true, std::memory_order_release); }
    void load_game_files();       // script thread, first tick: the base game's sound banks, then a reload
    // Pure helper: whether an already-seen game window closed or became hidden (not merely minimized).
    static bool window_is_closing(bool had_window, bool is_window, bool is_visible, bool is_iconic) {
        if (!had_window) return false;
        return !is_window || (!is_visible && !is_iconic);
    }
    // Pure helper: whether stalled-script gameplay should pass native game rumble through.
    static bool should_fallback_rumble(bool frozen, bool paused, double frozen_for, float rl, float rh, bool already_active) {
        if (!frozen || paused || frozen_for < 3.0) return false;
        return already_active || rl > 0.001f || rh > 0.001f;
    }
    static void apply_menu_idle(OutputState& out) {
        out.r2 = fx::off();
        out.l2 = fx::off();
        out.has_rumble = true;
        out.rumble_left = 0;
        out.rumble_right = 0;
        out.has_lightbar = true;
        out.r = 0;
        out.g = 0;
        out.b = 0;
        out.player_leds = 0;
        out.mute_led = 0;
    }
    // Pure helper: gate Cross confirm thump strictly to the loaded title menu and in-game pause menu.
    static bool allow_ui_confirm(bool ever_valid, double g_time, bool g_menu,
                                 bool texts_ready, bool intro_done, bool title_armed) {
        if (ever_valid) return g_menu;
        if (g_time > 0.0) return false;
        return texts_ready && intro_done && title_armed;
    }
    // test harness introspection
    std::string status();
    DualSense& pad() { return pad_; }
    AudioEngine& audio() { return audio_; }

private:
    bool on_input(const InputState& s, OutputState& out);
    void load_data();
    void apply_mic();
    void confirm();
    void build_engine(const nlohmann::json& profile, const std::string& name);

    std::atomic<bool> in_game_{false}, initialized_{false}, enabled_{false}, shutting_down_{false};
    std::atomic<int> reload_key_{120};
    std::atomic<float> wheel_click_gain_{0.6f};
    std::thread reconnect_thread_;
    std::mutex reconnect_mu_;
    double next_slot_check_ = 0;          // on_input: next look at the game's pad slot
    double ps_since_ = -1;                // PS held since (hold ~1 s = reload)
    double fight_until_ = 0;              // shots / hits lately: the everyday textures step back
    bool record_cues_ = false;            // research: log every effect as it plays (Record=1)
    bool ps_fired_ = false;
    bool frozen_ = false;                 // debug: the game state stopped updating (pause / loading)
    double frozen_log_t_ = 0;
    double bt_log_t_ = 0;                 // debug: the strongest rumble sent over Bluetooth, logged every 2 s
    int bt_max_l_ = 0, bt_max_r_ = 0;
    std::map<std::string, int> frozen_cues_;
    std::string test_weapon_;
    Calibration cal_;
    int wheel_r2_ = 0, wheel_l2_ = 0;      // last trigger positions, for the wheel's selector clicks
    bool wheel_open_ = false;
    double perf_t_ = 0, perf_sum_ = 0, perf_max_ = 0, perf_frame_max_ = 0;
    int perf_n_ = 0;
    uint32_t wheel_item_ = 0;              // last highlighted wheel item: a tick when it changes
    Settings settings_;
    Catalog catalog_;
    GameAudio game_audio_;
    HapticLibrary lib_;
    AudioEngine audio_;
    Features features_;
    DualSense pad_;
    Voice voice_;
    std::unique_ptr<WeaponEngine> engine_;
    uint32_t engine_key_ = 0;
    std::string engine_name_;
    std::map<std::string, int> learned_break_;
    std::atomic<bool> reload_requested_{false};
    std::atomic<bool> speaker_mechanics_{false};
    std::atomic<bool> speaker_ambient_{true}, speaker_ui_{true};
    std::vector<std::string> voice_greet_, voice_calm_, voice_stop_, voice_spur_;
    std::string voice_key_;           // the voice settings the recognizer runs with (restarted when they change)
    std::atomic<bool> whistle_on_{false};
    // a whistle waits ~1 s for the speech recognizer: if a spoken command ("chao") lands around it, it was a word
    std::atomic<double> whistle_at_{-1}, last_command_t_{-10};
    bool triggers_on_ = true, haptics_on_ = true;
    float trigger_strength_ = 1.0f;
    uint32_t prev_buttons_ = 0;
    std::mutex status_mu_;
    std::string last_state_;
    int last_rt_ = 0;
    bool reload_key_down_ = false;
    bool pad_was_connected_ = false, pad_seen_ = false;
    double l1_since_ = -1, wheel_until_ = 0;
    bool need_r2_release_ = false;
    float game_rumble_gain_ = 0.15f;
    bool frozen_warned_ = false;
    bool frozen_rumble_fallback_ = false;
    bool ever_valid_ = false;
    double last_ui_confirm_t_ = -10.0;
    double init_t_ = 0.0;
    bool title_cross_armed_ = true;
    std::atomic<bool> restarting_{false};
    std::atomic<bool> exit_reset_pending_{false};
    HWND game_hwnd_ = nullptr;
    double last_hwnd_check_ = -1.0;
    bool window_closing_ = false;
};

Core& core();

}  // namespace dsr2
