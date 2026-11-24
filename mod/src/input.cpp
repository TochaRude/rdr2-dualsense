#include "input.h"

#include "config.h"
#include "log.h"
#include "world.h"

namespace dsr2 {

Input& input() {
    static Input* i = new Input();
    return *i;
}

static DWORD WINAPI hk_get_state(DWORD idx, XINPUT_STATE* s) { return input().get_state(idx, s); }
static DWORD WINAPI hk_set_state(DWORD idx, XINPUT_VIBRATION* v) { return input().set_state(idx, v); }

// Executable page within +-2 GB of `target` for a 14-byte absolute jump.
static void* alloc_near(void* target) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t t = uintptr_t(target), gran = si.dwAllocationGranularity;
    for (uintptr_t d = gran; d < 0x70000000; d += gran) {
        uintptr_t cands[2] = {t > d ? t - d : 0, t + d};
        for (uintptr_t cand : cands) {
            if (!cand) continue;
            cand &= ~(gran - 1);
            void* p = VirtualAlloc(reinterpret_cast<void*>(cand), 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (p) return p;
        }
    }
    return nullptr;
}

// Redirect `target` to `detour` with an atomic 8-byte write (5-byte relative jump + 3 preserved bytes) to a relay.
static bool hook(void* target, void* detour) {
    if (!target) return false;
    auto* relay = static_cast<uint8_t*>(alloc_near(target));
    if (!relay) return false;
    relay[0] = 0xFF; relay[1] = 0x25;
    std::memset(relay + 2, 0, 4);
    std::memcpy(relay + 6, &detour, 8);
    DWORD relay_old = 0;
    if (!VirtualProtect(relay, 4096, PAGE_EXECUTE_READ, &relay_old)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), relay, 14);
    intptr_t diff = intptr_t(relay) - (intptr_t(target) + 5);
    if (diff < INT32_MIN || diff > INT32_MAX) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(target, 8, PAGE_EXECUTE_READWRITE, &old)) {
        VirtualFree(relay, 0, MEM_RELEASE);
        return false;
    }
    int64_t orig = 0;
    std::memcpy(&orig, target, 8);
    uint8_t patch[8];
    std::memcpy(patch, &orig, 8);
    int32_t rel = int32_t(diff);
    patch[0] = 0xE9;
    std::memcpy(patch + 1, &rel, 4);
    int64_t next = 0;
    std::memcpy(&next, patch, 8);
    InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(target), next, orig);
    VirtualProtect(target, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, 8);
    return true;
}

bool Input::install_hook() {
    HMODULE real = LoadLibraryW(L"xinput1_4.dll");
    if (real) {
        real_get_ = reinterpret_cast<decltype(real_get_)>(GetProcAddress(real, "XInputGetState"));
        real_set_ = reinterpret_cast<decltype(real_set_)>(GetProcAddress(real, "XInputSetState"));
    }
    HMODULE x = GetModuleHandleW(L"xinput9_1_0.dll");
    if (!x) x = LoadLibraryW(L"xinput9_1_0.dll");
    if (!x) {
        LOGE("xinput9_1_0.dll not loaded - cannot feed the game");
        return false;
    }
    bool a = hook(reinterpret_cast<void*>(GetProcAddress(x, "XInputGetState")), reinterpret_cast<void*>(&hk_get_state));
    bool b = hook(reinterpret_cast<void*>(GetProcAddress(x, "XInputSetState")), reinterpret_cast<void*>(&hk_set_state));
    LOGI("XInput hook: GetState %s, SetState %s", a ? "ok" : "FAILED", b ? "ok" : "FAILED");
    return a && b;
}

WORD xbutton_from_name(const std::string& n0) {
    std::string n;
    for (char c : n0) n += char(toupper((unsigned char)c));
    static const std::pair<const char*, WORD> map[] = {
        {"A", XINPUT_GAMEPAD_A}, {"B", XINPUT_GAMEPAD_B}, {"X", XINPUT_GAMEPAD_X}, {"Y", XINPUT_GAMEPAD_Y},
        {"LB", XINPUT_GAMEPAD_LEFT_SHOULDER}, {"RB", XINPUT_GAMEPAD_RIGHT_SHOULDER}, {"BACK", XINPUT_GAMEPAD_BACK},
        {"START", XINPUT_GAMEPAD_START}, {"LS", XINPUT_GAMEPAD_LEFT_THUMB}, {"RS", XINPUT_GAMEPAD_RIGHT_THUMB},
        {"UP", XINPUT_GAMEPAD_DPAD_UP}, {"DOWN", XINPUT_GAMEPAD_DPAD_DOWN}, {"LEFT", XINPUT_GAMEPAD_DPAD_LEFT},
        {"RIGHT", XINPUT_GAMEPAD_DPAD_RIGHT},
    };
    for (auto& [k, v] : map) if (n == k) return v;
    return 0;
}

void Input::configure() {
    Config& c = config();
    GyroCfg gc;
    std::string mode = c.str("Gyro", "Mode", "off");
    for (char& ch : mode) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    gc.mode = (mode == "always") ? 2 : (mode == "aim" || mode == "1" || mode == "on" || mode == "true") ? 1 : 0;
    gc.smooth_th = std::max(0.1f, c.num("Gyro", "SmoothThreshold", 6.0f));
    gc.sensitivity = c.num("Gyro", "Sensitivity", 1.0f);
    gc.vert_ratio = c.num("Gyro", "VerticalRatio", 0.75f);
    gc.stick_rate = std::max(10.0f, c.num("Gyro", "StickRate", 90.0f));
    gc.stick_dz = clampv(c.num("Gyro", "StickDeadzone", 0.2f), 0.0f, 0.5f);
    gc.invert_x = c.flag("Gyro", "InvertX", false) ? -1.0f : 1.0f;
    gc.invert_y = c.flag("Gyro", "InvertY", false) ? -1.0f : 1.0f;
    gc.stick_out = c.str("Gyro", "Output", "camera") == "stick";
    hide_other_ = c.flag("General", "HideOtherPads", true);
    world().gyro_enabled = gc.mode != 0;
    world().gyro_wants_always = gc.mode == 2;
    std::lock_guard<std::mutex> l(mu_);
    touch_click_ = xbutton_from_name(c.str("Touchpad", "Click", "BACK"));
    swipe_[0] = xbutton_from_name(c.str("Touchpad", "SwipeUp", ""));
    swipe_[1] = xbutton_from_name(c.str("Touchpad", "SwipeDown", ""));
    swipe_[2] = xbutton_from_name(c.str("Touchpad", "SwipeLeft", ""));
    swipe_[3] = xbutton_from_name(c.str("Touchpad", "SwipeRight", ""));
    two_finger_ = xbutton_from_name(c.str("Touchpad", "TwoFingerTap", ""));
    gyro_cfg_ = gc;
}

void Input::inject(WORD button, double seconds) {
    if (!button) return;
    std::lock_guard<std::mutex> l(mu_);
    injections_.push_back({button, now_s() + seconds});
}

void Input::hold_lt(double seconds) {
    std::lock_guard<std::mutex> l(mu_);
    lt_until_ = seconds > 0 ? now_s() + seconds : 0;
}

void Input::probe_lt(int value) {
    std::lock_guard<std::mutex> l(mu_);
    lt_probe_ = value;
}

static SHORT stick(int v, bool invert) {
    int s = v >= 128 ? (v - 128) * 32767 / 127 : (v - 128) * 32768 / 128;
    if (invert) s = -s;
    return SHORT(clampv(s, -32768, 32767));
}

void Input::publish(const InputState& s, int game_rt, bool connected) {
    XINPUT_GAMEPAD g{};
    auto map = [&](uint32_t m, WORD x) { if (s.buttons & m) g.wButtons |= x; };
    map(CROSS, XINPUT_GAMEPAD_A);
    map(CIRCLE, XINPUT_GAMEPAD_B);
    map(SQUARE, XINPUT_GAMEPAD_X);
    map(TRIANGLE, XINPUT_GAMEPAD_Y);
    map(L1, XINPUT_GAMEPAD_LEFT_SHOULDER);
    map(R1, XINPUT_GAMEPAD_RIGHT_SHOULDER);
    map(CREATE, XINPUT_GAMEPAD_BACK);
    map(OPTIONS, XINPUT_GAMEPAD_START);
    map(L3, XINPUT_GAMEPAD_LEFT_THUMB);
    map(R3, XINPUT_GAMEPAD_RIGHT_THUMB);
    map(DPAD_UP, XINPUT_GAMEPAD_DPAD_UP);
    map(DPAD_DOWN, XINPUT_GAMEPAD_DPAD_DOWN);
    map(DPAD_LEFT, XINPUT_GAMEPAD_DPAD_LEFT);
    map(DPAD_RIGHT, XINPUT_GAMEPAD_DPAD_RIGHT);
    g.bLeftTrigger = BYTE(s.l2);
    g.bRightTrigger = BYTE(game_rt >= 0 ? game_rt : s.r2);
    g.sThumbLX = stick(s.lx, false);
    g.sThumbLY = stick(s.ly, true);
    raw_rx_ = s.rx;
    raw_ry_ = s.ry;
    g.sThumbRX = stick(s.rx, false);
    g.sThumbRY = stick(s.ry, true);
    // gyro rides on the right stick: it adds to your stick instead of fighting it (and the game ignores
    // scripted camera pitch while aiming)
    if (world().gyro_enabled.load()) {
        if (float gx = gyro_rx_.load(); gx != 0)
            g.sThumbRX = SHORT(clampv(int(g.sThumbRX) + int(gx * 32767.0f), -32768, 32767));
        if (float gy = gyro_ry_.load(); gy != 0)
            g.sThumbRY = SHORT(clampv(int(g.sThumbRY) + int(gy * 32767.0f), -32768, 32767));
    }
    double t = now_s();
    std::lock_guard<std::mutex> l(mu_);
    if (s.buttons & TOUCHPAD) g.wButtons |= touch_click_;
    for (auto it = injections_.begin(); it != injections_.end();) {
        if (t < it->until) { g.wButtons |= it->button; ++it; }
        else it = injections_.erase(it);
    }
    if (t < lt_until_) g.bLeftTrigger = 255;          // the mod is holding L2 (focus for a greeting)
    if (lt_probe_ >= 0) g.bLeftTrigger = BYTE(lt_probe_);   // aim threshold measurement (world.cpp, F11)
    connected_ = connected;
    if (std::memcmp(&g, &state_.Gamepad, sizeof g) != 0) {
        state_.Gamepad = g;
        state_.dwPacketNumber = ++packet_;
    }
}

DWORD Input::get_state(DWORD idx, XINPUT_STATE* out) {
    if (!out) return ERROR_BAD_ARGUMENTS;
    {
        std::lock_guard<std::mutex> l(mu_);
        if (connected_) {
            if (idx == 0) {
                *out = state_;
                if (state_.Gamepad.bRightTrigger >= 128) rt_press_read_t_ = now_s();   // debug: the game saw R2 pressed
                return ERROR_SUCCESS;
            }
            if (hide_other_.load()) return ERROR_DEVICE_NOT_CONNECTED;
        }
    }
    return real_get_ ? real_get_(idx, out) : ERROR_DEVICE_NOT_CONNECTED;   // passthrough outside mu_
}

DWORD Input::set_state(DWORD idx, XINPUT_VIBRATION* v) {
    if (!v) return ERROR_BAD_ARGUMENTS;
    bool ours;
    {
        std::lock_guard<std::mutex> l(mu_);
        ours = connected_;
    }
    if (ours) {
        if (idx == 0) {
            rumble_low_ = v->wLeftMotorSpeed / 65535.0f;
            rumble_high_ = v->wRightMotorSpeed / 65535.0f;
            return ERROR_SUCCESS;
        }
        if (hide_other_.load()) return ERROR_DEVICE_NOT_CONNECTED;
    }
    return real_set_ ? real_set_(idx, v) : ERROR_DEVICE_NOT_CONNECTED;
}

// Swipes (one finger, quick, long enough) and a two-finger tap become button presses.
void Input::track_touch(const InputState& s) {
    const bool any = s.touch[0].active || s.touch[1].active;
    const bool both = s.touch[0].active && s.touch[1].active;
    const TouchPoint* primary = nullptr;
    if (touching_) {
        for (int i = 0; i < 2; ++i)
            if (s.touch[i].active && s.touch[i].id == touch_id_) primary = &s.touch[i];
    } else if (any) {
        primary = s.touch[0].active ? &s.touch[0] : &s.touch[1];
        touching_ = true;
        touch_id_ = primary->id;
        sx_ = lx_ = primary->x;
        sy_ = ly_ = primary->y;
        touch_t_ = s.t;
        two_ = both;
    }
    if (touching_) {
        if (both) two_ = true;
        if (primary) {
            lx_ = primary->x;
            ly_ = primary->y;
        }
        if (!any) {
            touching_ = false;
            touch_id_ = -1;
            double dt = s.t - touch_t_;
            int dx = lx_ - sx_, dy = ly_ - sy_;
            WORD two_btn, sw[4];
            {
                std::lock_guard<std::mutex> l(mu_);
                two_btn = two_finger_;
                for (int i = 0; i < 4; ++i) sw[i] = swipe_[i];
            }
            if (two_ && dt < 0.35 && std::abs(dx) < 150 && std::abs(dy) < 150) {
                inject(two_btn, 0.12);
            } else if (!two_ && dt < 0.5) {
                if (std::abs(dy) > 300 && std::abs(dy) > std::abs(dx)) inject(sw[dy < 0 ? 0 : 1], 0.12);
                else if (std::abs(dx) > 450) inject(sw[dx < 0 ? 2 : 3], 0.12);
            }
        }
    }
}

// Gyro aim: a fine-tuning aid on top of the right stick, only while free-aiming (never during the game's
// lock-on). Uses gravity-projected player-space yaw and dt-scaled smoothing/bias learning so USB (250 Hz)
// and Bluetooth (~60 Hz) feel identical.
void Input::track_gyro(const InputState& s, bool aiming, bool can_gyro) {
    GyroCfg gc;
    {
        std::lock_guard<std::mutex> l(mu_);
        gc = gyro_cfg_;
    }
    const bool active = (gc.mode == 2 && can_gyro) || (gc.mode == 1 && aiming);
    if (!active) {
        aim_since_ = -1;
        aim_fade_ = 0;
        gyro_rx_ = 0;
        gyro_ry_ = 0;
        smooth_yaw_ = smooth_pitch_ = 0;
    }
    double dt = gyro_t_ > 0 ? s.t - gyro_t_ : 0;
    gyro_t_ = s.t;
    if (dt <= 0 || dt > 0.05) return;
    float r[3];
    for (int i = 0; i < 3; ++i) r[i] = float(s.gyro[i]) / 16.4f;   // deg/s
    if (still_time_ < 0 && std::fabs(r[0]) + std::fabs(r[1]) + std::fabs(r[2]) < 15.0f) {
        for (int i = 0; i < 3; ++i) bias_[i] = r[i];
        still_time_ = 0;
    }
    if (!active) {
        float mag = 0;
        for (int i = 0; i < 3; ++i) mag += std::fabs(r[i] - bias_[i]);
        if (mag < 10.0f && still_time_ >= 0) {       // resting: dt-scaled drift learning (never while aiming)
            still_time_ += float(dt);
            if (still_time_ > 0.25f) {
                float alpha = 1.0f - std::exp(-float(dt) / 0.15f);
                for (int i = 0; i < 3; ++i) bias_[i] += (r[i] - bias_[i]) * alpha;
            }
        } else {
            still_time_ = 0;
        }
        return;
    }
    if (aim_since_ < 0) aim_since_ = s.t;
    if (s.t - aim_since_ < 0.35) return;
    aim_fade_ = std::min(1.0f, aim_fade_ + float(dt) / 0.2f);

    float gx = r[0] - bias_[0], gy = r[1] - bias_[1], gz = r[2] - bias_[2];
    // Player-space yaw: project gyro onto normalized gravity vector (accel y/z) so reclined posture stays level
    float yaw = gy, pitch = gx;
    float ax = float(s.accel[0]), ay = float(s.accel[1]), az = float(s.accel[2]);
    float alen = std::sqrt(ax * ax + ay * ay + az * az);
    if (alen > 2000.0f) {
        float ny = ay / alen, nz = az / alen;
        float horiz = std::sqrt(ny * ny + nz * nz);
        if (horiz > 0.3f) yaw = (gy * ny + gz * nz) / horiz;
    }
    // dt-independent soft tiered smoothing: tiny tremors smoothed (tau = 32 ms), deliberate turns pass straight through
    float th = gc.smooth_th;
    float ema = 1.0f - std::exp(-float(dt) / 0.032f);
    auto split = [&](float v, float& sm) {
        float direct = clampv((std::fabs(v) - th * 0.5f) / (th * 0.5f), 0.0f, 1.0f);
        sm += (v * (1.0f - direct) - sm) * ema;
        return v * direct + sm;
    };
    float y = split(yaw, smooth_yaw_);
    float pch = split(pitch, smooth_pitch_);
    if (std::fabs(y) < 0.25f) y = 0;
    if (std::fabs(pch) < 0.25f) pch = 0;

    float sens = gc.sensitivity * aim_fade_;
    auto to_stick = [&](float rate) {
        float v = std::min(1.0f, std::fabs(rate) / gc.stick_rate);
        return v < 1e-3f ? 0.0f : std::copysign(gc.stick_dz + (1.0f - gc.stick_dz) * v, rate);
    };
    bool stick_busy = std::abs(int(s.rx) - 128) > 20 || std::abs(int(s.ry) - 128) > 20;
    world().gyro_stick_busy = stick_busy;
    if (gc.stick_out) {
        gyro_rx_ = to_stick(-y * sens * gc.invert_x);
        gyro_ry_ = to_stick(pch * sens * gc.vert_ratio * gc.invert_y);
    } else {
        gyro_rx_ = 0;
        gyro_ry_ = 0;
        if (!stick_busy) world().add_gyro(y * float(dt) * sens * gc.invert_x, pch * float(dt) * sens * gc.vert_ratio * gc.invert_y);
    }
    if (s.t - gyro_log_t_ > 0.5) {
        gyro_log_t_ = s.t;
        LOGD("gyro: raw %+.1f %+.1f %+.1f deg/s, bias %+.1f %+.1f %+.1f -> yaw %+.1f pitch %+.1f -> stick x %+.2f y %+.2f",
             r[0], r[1], r[2], bias_[0], bias_[1], bias_[2], y, pch, gyro_rx_.load(), gyro_ry_.load());
    }
}

}  // namespace dsr2
