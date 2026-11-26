// DualSense HID transport: input parsing, output reports (USB + Bluetooth), hot-plug.
#pragma once

#include "common.h"
#include "effects.h"

#include <mutex>
#include <thread>

namespace dsr2 {

// Flat button mask (same bits as the lab).
enum Btn : uint32_t {
    SQUARE = 1u << 0, CROSS = 1u << 1, CIRCLE = 1u << 2, TRIANGLE = 1u << 3,
    L1 = 1u << 4, R1 = 1u << 5, L2_BTN = 1u << 6, R2_BTN = 1u << 7,
    CREATE = 1u << 8, OPTIONS = 1u << 9, L3 = 1u << 10, R3 = 1u << 11,
    PS = 1u << 12, TOUCHPAD = 1u << 13, MUTE = 1u << 14,
    DPAD_UP = 1u << 15, DPAD_RIGHT = 1u << 16, DPAD_DOWN = 1u << 17, DPAD_LEFT = 1u << 18,
};

struct TouchPoint {
    bool active = false;
    int id = 0;
    int x = 0;      // 0..1919
    int y = 0;      // 0..1079
};

struct InputState {
    double t = 0;
    int lx = 128, ly = 128, rx = 128, ry = 128;
    int l2 = 0, r2 = 0;
    uint32_t buttons = 0;
    int r2_feedback = 0, l2_feedback = 0, effect_state = 0;
    int battery = 0;               // 0-10
    int battery_state = 0;         // 0 discharging, 1 charging, 2 full (high nibble of status[0])
    bool charging() const { return battery_state == 1 || battery_state == 2; }
    bool headphones = false;       // something plugged into the controller's headphone jack
    int16_t gyro[3] = {0, 0, 0};   // pitch, yaw, roll  (~16 LSB per deg/s)
    int16_t accel[3] = {0, 0, 0};
    uint32_t sensor_ts = 0;
    TouchPoint touch[2];
    bool fresh = false;            // a new report arrived (false = timing tick only)
    bool pressed(uint32_t m) const { return (buttons & m) != 0; }
};

struct OutputState {
    fx::Block r2 = fx::off();
    fx::Block l2 = fx::off();
    bool has_rumble = false;
    uint8_t rumble_left = 0, rumble_right = 0;
    bool has_lightbar = false;
    uint8_t r = 0, g = 0, b = 0;
    int player_leds = -1;          // 5-bit mask, -1 = leave alone
    int mute_led = -1;             // 0 off, 1 on, 2 pulse, -1 = leave alone
    int trigger_power_reduction = 0;
    bool speaker_enabled = false;
};

class DualSense {
public:
    // Called on the I/O thread for every input report (or ~every 10 ms); fill `out` and return true to send it.
    using Handler = std::function<bool(const InputState&, OutputState& out)>;

    ~DualSense() { stop(); }
    bool start(Handler h);
    void stop();
    bool connected() const { return connected_.load(); }
    bool usb() const { return usb_.load(); }
    float report_rate() const { return rate_.load(); }
    std::string name() const;
    // Bluetooth haptics: one report 0x32 with 64 bytes of audio (32 stereo frames, 3 kHz, 8 bit). Any thread.
    bool send_haptics(const uint8_t* pcm64);
    // Exposed for unit testing of HID payload/CRC encoding.
    static uint32_t bt_crc32(const uint8_t* data, size_t n);
    void encode_payload(const OutputState& o, uint8_t* p) { build_payload(o, p); }
    static OutputState reset_output();
    static void build_reset_payload(uint8_t* p, bool force_usb_mute, bool release_lightbar);
    void reset_on_exit() noexcept;

private:
    struct GyroCal {
        bool valid = false;
        float bias[3] = {0, 0, 0};
        float scale[3] = {1, 1, 1};  // maps raw LSB -> normalized 16.4 LSB/(deg/s)
    };
    void loop();
    bool open();
    void close();
    bool write(const OutputState& o, bool force);
    void build_payload(const OutputState& o, uint8_t* p);
    void parse_calibration(const uint8_t* feat);

    Handler handler_;
    std::thread thread_;
    std::atomic<bool> running_{false}, connected_{false}, usb_{true};
    std::atomic<float> rate_{0};
    HANDLE dev_ = INVALID_HANDLE_VALUE;
    std::atomic<HANDLE> raw_dev_{INVALID_HANDLE_VALUE};
    HANDLE read_ev_ = nullptr, write_ev_ = nullptr, hap_ev_ = nullptr;
    std::atomic<HANDLE> exit_ev_{nullptr};
    mutable std::mutex dev_mu_;          // protects dev_, name_, write_buf_, hap_buf_
    uint8_t hap_counter_ = 0;
    size_t in_len_ = 64, out_len_ = 48;
    std::atomic<size_t> raw_out_len_{48};
    std::atomic<uint8_t> bt_seq_{0};
    std::atomic<bool> lightbar_released_{false};
    // audio output of the controller (USB): -1 unknown, 0 muted, 1 headphone jack, 2 built-in speaker. The
    // controller starts on the jack even with nothing plugged in, so the speaker stays silent until routed.
    std::atomic<int> routed_{-1};
    int routing_ = -1;
    bool headphones_ = false;
    GyroCal gyro_cal_;
    std::vector<uint8_t> last_payload_, write_buf_, hap_buf_;
    InputState last_;
    std::string name_;
};

}  // namespace dsr2
