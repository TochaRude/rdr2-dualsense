// What the game reads as its gamepad: the DualSense mapped to XInput (via a hook on
// xinput9_1_0!XInputGetState), the shot moment decided by the trigger engine, injected presses
// (whistle, touchpad gestures), the game's rumble captured from XInputSetState, gyro integration.
#pragma once

#include "common.h"
#include "ds5.h"

#include <xinput.h>

namespace dsr2 {

class Input {
public:
    bool install_hook();                         // patch xinput9_1_0 in the game process
    // controller thread: publish the state the game will read
    void publish(const InputState& s, int game_rt, bool connected);
    void inject(WORD xbutton, double seconds);   // e.g. XINPUT_GAMEPAD_DPAD_UP for the whistle
    void hold_lt(double seconds);                // hold L2 fully for `seconds` (0 = release now)
    void probe_lt(int value);                    // the game sees this L2 value (0-255; -1 = the real one)
    void configure();                            // read touchpad mapping from the ini
    // gestures + gyro, called by the controller thread for each fresh report
    void track_touch(const InputState& s);
    void track_gyro(const InputState& s, bool aiming_hint, bool can_gyro = true);
    void reset_gyro_state();
    float gyro_rx() const { return gyro_rx_.load(); }
    float gyro_ry() const { return gyro_ry_.load(); }

    // game rumble (XInputSetState) 0..1
    float rumble_low() const { return rumble_low_.load(); }
    double rt_press_read_t() const { return rt_press_read_t_.load(); }   // when the game last read R2 pressed
    float rumble_high() const { return rumble_high_.load(); }
    void raw_rstick(int& rx, int& ry) const { rx = raw_rx_.load(); ry = raw_ry_.load(); }

    // hook targets
    DWORD get_state(DWORD idx, XINPUT_STATE* out);
    DWORD set_state(DWORD idx, XINPUT_VIBRATION* v);

private:
    std::mutex mu_;
    XINPUT_STATE state_{};
    bool connected_ = false;
    DWORD packet_ = 0;
    struct Injection { WORD button; double until; };
    std::vector<Injection> injections_;
    double lt_until_ = 0;
    int lt_probe_ = -1;
    std::atomic<float> rumble_low_{0}, rumble_high_{0};
    std::atomic<int> raw_rx_{128}, raw_ry_{128};
    std::atomic<double> rt_press_read_t_{0};
    struct TouchActionCfg {
        WORD buttons = 0;
        double hold_s = 0.15;
    };
    TouchActionCfg touch_click_{XINPUT_GAMEPAD_BACK, 0.15}, swipe_[4]{}, two_finger_{};
    bool touch_clicked_ = false, prev_touchpad_btn_ = false;
    // cached gyro config (updated in configure() so track_gyro() never locks Config at 250 Hz)
    struct GyroCfg {
        int mode = 0;            // 0 = off, 1 = aim, 2 = always
        float smooth_th = 6.0f;
        float sensitivity = 1.0f;
        float vert_ratio = 0.75f;
        float stick_rate = 90.0f;
        float stick_dz = 0.2f;
        float invert_x = 1.0f, invert_y = 1.0f;
        bool stick_out = false;
    } gyro_cfg_;
    // touch tracking
    bool touching_ = false;
    int touch_id_ = -1, sx_ = 0, sy_ = 0, lx_ = 0, ly_ = 0;
    double touch_t_ = 0;
    bool two_ = false;
    // gyro
    float bias_[3] = {0, 0, 0};
    float still_ref_[3] = {0, 0, 0};
    bool still_ref_ok_ = false;
    float still_time_ = 0.0f, smooth_yaw_ = 0, smooth_pitch_ = 0;
    float aim_fade_ = 0;
    double gyro_log_t_ = 0, aim_since_ = -1;
    std::atomic<float> gyro_rx_{0}, gyro_ry_{0};   // gyro, added to the right stick the game reads (-1..1)
    double gyro_t_ = 0;
    std::atomic<bool> hide_other_{true};
    // real XInput for passthrough when the DualSense is absent
    DWORD (WINAPI* real_get_)(DWORD, XINPUT_STATE*) = nullptr;
    DWORD (WINAPI* real_set_)(DWORD, XINPUT_VIBRATION*) = nullptr;
};

struct TouchAction {
    WORD buttons = 0;
    double hold_s = 0.15;
};

Input& input();
TouchAction touch_action_from_name(const std::string& name);
WORD xbutton_from_name(const std::string& name);

}  // namespace dsr2
