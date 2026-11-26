#include "ds5.h"

#include "log.h"

#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}

namespace dsr2 {

static constexpr uint16_t kSony = 0x054C;
static constexpr uint16_t kDualSense = 0x0CE6, kDualSenseEdge = 0x0DF2;

static uint32_t crc32_table[256];
static std::once_flag crc_once;
static std::atomic<bool> crc_ready{false};
static void crc_init() {
    std::call_once(crc_once, [] {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            crc32_table[i] = c;
        }
        crc_ready.store(true, std::memory_order_release);
    });
}
static uint32_t crc32(const uint8_t* d, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    if (!crc_ready.load(std::memory_order_acquire)) crc_init();
    for (size_t i = 0; i < n; ++i) crc = crc32_table[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

uint32_t DualSense::bt_crc32(const uint8_t* data, size_t n) {
    uint8_t seed = 0xA2;
    return ~crc32(data, n, crc32(&seed, 1));
}

std::string DualSense::name() const {
    std::lock_guard<std::mutex> l(dev_mu_);
    return name_;
}

bool DualSense::start(Handler h) {
    crc_init();
    handler_ = std::move(h);
    running_ = true;
    read_ev_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    write_ev_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    hap_ev_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE exit_ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    exit_ev_.store(exit_ev, std::memory_order_release);
    thread_ = std::thread([this] { loop(); });
    return true;
}

void DualSense::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (read_ev_) CloseHandle(read_ev_);
    if (write_ev_) CloseHandle(write_ev_);
    if (hap_ev_) CloseHandle(hap_ev_);
    HANDLE exit_ev = exit_ev_.exchange(nullptr, std::memory_order_acq_rel);
    if (exit_ev) CloseHandle(exit_ev);
    read_ev_ = write_ev_ = hap_ev_ = nullptr;
}

void DualSense::parse_calibration(const uint8_t* feat) {
    // DualSense feature report 0x05 layout (Linux hid-playstation):
    // [1..6]: gyro pitch/yaw/roll bias (int16 LE)
    // [7..18]: pitch+, pitch-, yaw+, yaw-, roll+, roll- (int16 LE)
    // [19..22]: speed+, speed- (int16 LE)
    auto i16 = [&](int off) -> int { return int(int16_t(feat[off] | (feat[off + 1] << 8))); };
    int speed_2x = i16(19) + i16(21);
    if (speed_2x <= 0 || speed_2x > 10000) {
        gyro_cal_.valid = false;
        return;
    }
    GyroCal cal;
    for (int a = 0; a < 3; ++a) {
        int bias = i16(1 + 2 * a);
        int range = i16(7 + 4 * a) - i16(9 + 4 * a);
        if (std::abs(bias) > 2048 || range <= 1000) {
            gyro_cal_.valid = false;
            return;
        }
        // Real deg/s = (raw - bias) * speed_2x / range. Normalize to 16.4 LSB/(deg/s).
        cal.bias[a] = float(bias);
        cal.scale[a] = (float(speed_2x) / float(range)) * 16.4f;
        if (!std::isfinite(cal.scale[a]) || cal.scale[a] < 0.7f || cal.scale[a] > 1.4f) {
            gyro_cal_.valid = false;
            return;
        }
    }
    cal.valid = true;
    gyro_cal_ = cal;
}

bool DualSense::open() {
    routed_.store(-1, std::memory_order_release);     // a (re)connected controller starts on the headphone jack again
    headphones_ = false;
    last_ = InputState{};
    GUID hid;
    HidD_GetHidGuid(&hid);
    HDEVINFO set = SetupDiGetClassDevsW(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return false;
    struct Cand { std::wstring path; bool usb; size_t in, out, feat; };
    std::vector<Cand> cands;
    SP_DEVICE_INTERFACE_DATA ifd{sizeof(ifd)};
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(set, nullptr, &hid, i, &ifd); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
        if (need < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
        std::vector<uint8_t> buf(need);
        auto* det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, nullptr, nullptr)) continue;
        HANDLE h = CreateFileW(det->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        HIDD_ATTRIBUTES a{sizeof(a)};
        bool ok = HidD_GetAttributes(h, &a) && a.VendorID == kSony && (a.ProductID == kDualSense || a.ProductID == kDualSenseEdge);
        PHIDP_PREPARSED_DATA pp = nullptr;
        HIDP_CAPS caps{};
        if (ok && HidD_GetPreparsedData(h, &pp)) {
            HidP_GetCaps(pp, &caps);
            HidD_FreePreparsedData(pp);
            ok = caps.UsagePage == 1 && caps.Usage == 5;
        } else {
            ok = false;
        }
        CloseHandle(h);
        if (ok) cands.push_back({det->DevicePath, caps.InputReportByteLength == 64, caps.InputReportByteLength,
                                 caps.OutputReportByteLength, caps.FeatureReportByteLength});
    }
    SetupDiDestroyDeviceInfoList(set);
    std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.usb > b.usb; });
    for (auto& c : cands) {
        HANDLE h = CreateFileW(c.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        usb_ = c.usb;
        in_len_ = c.in;
        out_len_ = c.out;
        raw_out_len_.store(c.out, std::memory_order_release);
        write_buf_.assign(std::max<size_t>(out_len_, c.usb ? 48 : 78), 0);
        hap_buf_.assign(std::max<size_t>(out_len_, 142), 0);
        gyro_cal_ = GyroCal{};
        {
            size_t flen = std::max<size_t>(c.feat, c.usb ? 64 : 41);
            std::vector<uint8_t> feat(flen, 0);
            feat[0] = 0x05;          // calibration report (also switches BT to full 0x31 reports)
            if (HidD_GetFeature(h, feat.data(), ULONG(feat.size()))) {
                parse_calibration(feat.data());
            } else if (!c.usb && feat.size() != 41) {
                uint8_t feat41[41] = {0x05};
                if (HidD_GetFeature(h, feat41, 41)) parse_calibration(feat41);
            }
        }
        lightbar_released_.store(false, std::memory_order_release);
        last_payload_.clear();
        {
            std::lock_guard<std::mutex> l(dev_mu_);
            dev_ = h;
            raw_dev_.store(h, std::memory_order_release);
            name_ = std::string("DualSense ") + (c.usb ? "USB" : "Bluetooth");
        }
        LOGI("controller: %s (in %zu, out %zu bytes%s)", name().c_str(), in_len_, out_len_,
             gyro_cal_.valid ? ", gyro cal ok" : "");
        return true;
    }
    return false;
}

void DualSense::close() {
    connected_ = false;
    HANDLE h = INVALID_HANDLE_VALUE;
    {
        std::lock_guard<std::mutex> l(dev_mu_);
        h = dev_;
    }
    if (h != INVALID_HANDLE_VALUE) {
        write(reset_output(), true);
        std::lock_guard<std::mutex> l(dev_mu_);
        HANDLE owned = raw_dev_.exchange(INVALID_HANDLE_VALUE, std::memory_order_acq_rel);
        if (owned != INVALID_HANDLE_VALUE) {
            CancelIoEx(owned, nullptr);
            CloseHandle(owned);
        } else if (dev_ != INVALID_HANDLE_VALUE) {
            CancelIoEx(dev_, nullptr);
            CloseHandle(dev_);
        }
        dev_ = INVALID_HANDLE_VALUE;
    }
}

static void parse(const uint8_t* d, InputState& s) {
    s.lx = d[0]; s.ly = d[1]; s.rx = d[2]; s.ry = d[3]; s.l2 = d[4]; s.r2 = d[5];
    uint8_t b0 = d[7], b1 = d[8], b2 = d[9];
    static const uint32_t hat[8] = {DPAD_UP, DPAD_UP | DPAD_RIGHT, DPAD_RIGHT, DPAD_RIGHT | DPAD_DOWN, DPAD_DOWN,
                                    DPAD_DOWN | DPAD_LEFT, DPAD_LEFT, DPAD_LEFT | DPAD_UP};
    uint32_t m = (b0 & 0x0F) < 8 ? hat[b0 & 0x0F] : 0;
    if (b0 & 0x10) m |= SQUARE;
    if (b0 & 0x20) m |= CROSS;
    if (b0 & 0x40) m |= CIRCLE;
    if (b0 & 0x80) m |= TRIANGLE;
    static const uint32_t row1[8] = {L1, R1, L2_BTN, R2_BTN, CREATE, OPTIONS, L3, R3};
    for (int i = 0; i < 8; ++i) if (b1 & (1 << i)) m |= row1[i];
    if (b2 & 1) m |= PS;
    if (b2 & 2) m |= TOUCHPAD;
    if (b2 & 4) m |= MUTE;
    s.buttons = m;
    for (int i = 0; i < 3; ++i) {
        s.gyro[i] = int16_t(d[15 + 2 * i] | (d[16 + 2 * i] << 8));
        s.accel[i] = int16_t(d[21 + 2 * i] | (d[22 + 2 * i] << 8));
    }
    s.sensor_ts = d[27] | (d[28] << 8) | (d[29] << 16) | (uint32_t(d[30]) << 24);
    for (int i = 0; i < 2; ++i) {
        const uint8_t* p = d + 32 + 4 * i;
        s.touch[i].active = !(p[0] & 0x80);
        s.touch[i].id = p[0] & 0x7F;
        s.touch[i].x = p[1] | ((p[2] & 0x0F) << 8);
        s.touch[i].y = (p[2] >> 4) | (p[3] << 4);
    }
    s.r2_feedback = d[41];
    s.l2_feedback = d[42];
    s.effect_state = d[47];
    s.battery = std::min(10, d[52] & 0x0F);
    s.battery_state = (d[52] >> 4) & 0x0F;
    s.headphones = (d[53] & 0x01) != 0;      // status[1] bit 0 (Linux hid-playstation: DS_STATUS1_HP_DETECT)
}

void DualSense::build_payload(const OutputState& o, uint8_t* p) {
    std::memset(p, 0, 47);
    p[0] = 0x04 | 0x08;                       // both trigger effects
    p[1] = 0x40;                              // motor power level
    if (o.has_rumble) {
        p[0] |= 0x01 | 0x02;
        p[38] |= 0x04;
        p[3] = o.rumble_left;
        p[2] = o.rumble_right;
    }
    std::memcpy(p + 10, o.r2.data(), 11);
    std::memcpy(p + 21, o.l2.data(), 11);
    p[36] = uint8_t(o.trigger_power_reduction & 7);         // low nibble: trigger motor power reduction (high: rumble)
    if (o.mute_led >= 0) {
        p[1] |= 0x01;
        p[8] = uint8_t(o.mute_led);
    }
    if (o.has_lightbar) {
        p[1] |= 0x04;
        if (!lightbar_released_.load(std::memory_order_acquire)) {
            p[38] |= 0x02;
            p[41] = 0x02;
        }
        p[44] = o.r; p[45] = o.g; p[46] = o.b;
    }
    if (o.player_leds >= 0) {
        p[1] |= 0x10;
        p[43] = uint8_t(o.player_leds & 0x1F);
    }
    // Audio output path (as the Linux hid-playstation driver does it): sent whenever it differs from what the
    // controller last accepted. Speaker: path 3 (right channel -> speaker), volume 0x64 (100%, accepted range
    // 0x3D-0x64), preamp +6 dB; when speaker_enabled is false, mute hardware speaker volume (0x00) and route to jack.
    // Headphones: path 0 (left/right -> jack).
    routing_ = -1;
    const int want_route = headphones_ ? 1 : (o.speaker_enabled ? 2 : 0);
    if (usb_ && want_route != routed_.load(std::memory_order_acquire)) {
        routing_ = want_route;
        p[0] |= 0x80;                         // audio control
        if (headphones_) {
            p[7] = 0x00;
        } else if (o.speaker_enabled) {
            p[7] = 0x30;
            p[0] |= 0x20;                     // speaker volume
            p[5] = 0x64;
            p[1] |= 0x80;                     // audio control 2
            p[37] = 0x02;                     // speaker preamp gain
        } else {
            p[7] = 0x00;                      // disable internal speaker path
            p[0] |= 0x20;                     // speaker volume
            p[5] = 0x00;                      // hardware speaker mute
        }
    }
}

OutputState DualSense::reset_output() {
    OutputState off;
    off.r2 = fx::off();
    off.l2 = fx::off();
    off.has_rumble = true;
    off.rumble_left = 0;
    off.rumble_right = 0;
    off.has_lightbar = true;
    off.r = 0; off.g = 0; off.b = 0;
    off.player_leds = 0;
    off.mute_led = 0;
    off.speaker_enabled = false;
    return off;
}

void DualSense::build_reset_payload(uint8_t* p, bool force_usb_mute, bool release_lightbar) {
    std::memset(p, 0, 47);
    p[0] = 0x01 | 0x02 | 0x04 | 0x08;         // rumble (0,0) + both trigger effects (off)
    p[1] = 0x01 | 0x04 | 0x10 | 0x40;         // mute LED (0) + lightbar (0,0,0) + player LEDs (0) + motor power
    p[38] = 0x04;                             // enable rumble motors (0,0)
    if (release_lightbar) {
        p[38] |= 0x02;
        p[41] = 0x02;
    }
    const fx::Block off = fx::off();
    std::memcpy(p + 10, off.data(), 11);
    std::memcpy(p + 21, off.data(), 11);
    if (force_usb_mute) {
        p[0] |= 0x80 | 0x20;                  // audio control + speaker volume
        p[5] = 0x00;                          // hardware speaker mute
        p[7] = 0x00;                          // disable internal speaker path
    }
}

void DualSense::reset_on_exit() noexcept {
    running_.store(false, std::memory_order_release);
    connected_.store(false, std::memory_order_release);
    HANDLE h = raw_dev_.exchange(INVALID_HANDLE_VALUE, std::memory_order_acq_rel);
    if (h == INVALID_HANDLE_VALUE) return;

    // Abort any pending overlapped HID reads/writes stopped when threads exited, then send a lock-free reset.
    CancelIoEx(h, nullptr);

    const bool is_usb = usb_.load(std::memory_order_acquire);
    const bool mute_spk = is_usb && routed_.load(std::memory_order_acquire) == 2;
    const bool release_lb = !lightbar_released_.load(std::memory_order_acquire);
    uint8_t payload[47];
    build_reset_payload(payload, mute_spk, release_lb);

    const size_t min_len = is_usb ? 48 : 78;
    const size_t out_hint = raw_out_len_.load(std::memory_order_acquire);
    uint8_t rep[256];
    const size_t want = std::min(sizeof(rep), std::max(out_hint, min_len));
    HANDLE ev = exit_ev_.load(std::memory_order_acquire);

    auto send_once = [&]() {
        std::memset(rep, 0, want);
        if (is_usb) {
            rep[0] = 0x02;
            std::memcpy(rep + 1, payload, 47);
        } else {
            const uint8_t seq = bt_seq_.fetch_add(1, std::memory_order_relaxed) & 0x0F;
            rep[0] = 0x31;
            rep[1] = uint8_t(seq << 4);
            rep[2] = 0x10;
            std::memcpy(rep + 3, payload, 47);
            uint32_t crc = bt_crc32(rep, 74);
            std::memcpy(rep + 74, &crc, 4);
        }
        OVERLAPPED ov{};
        ov.hEvent = ev;
        if (ev) ResetEvent(ev);
        DWORD n = 0;
        BOOL ok = WriteFile(h, rep, DWORD(want), &n, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            HANDLE wait_h = ev ? ev : h;
            if (WaitForSingleObject(wait_h, 80) != WAIT_OBJECT_0) {
                CancelIoEx(h, &ov);
                GetOverlappedResult(h, &ov, &n, TRUE);
            } else {
                GetOverlappedResult(h, &ov, &n, FALSE);
            }
        }
    };

    send_once();
    if (!is_usb) {
        Sleep(15);
        send_once();
    }
    CloseHandle(h);
}

bool DualSense::write(const OutputState& o, bool force) {
    uint8_t payload[47];
    build_payload(o, payload);
    if (!force && last_payload_.size() == 47 && std::memcmp(last_payload_.data(), payload, 47) == 0) return true;
    std::lock_guard<std::mutex> l(dev_mu_);
    if (dev_ == INVALID_HANDLE_VALUE) return false;
    size_t want = std::max<size_t>(out_len_, usb_ ? 48 : 78);
    if (write_buf_.size() != want) write_buf_.resize(want);
    std::fill(write_buf_.begin(), write_buf_.end(), 0);
    auto& rep = write_buf_;
    if (usb_) {
        rep[0] = 0x02;
        std::memcpy(rep.data() + 1, payload, 47);
    } else {
        rep[0] = 0x31;
        const uint8_t seq = bt_seq_.fetch_add(1, std::memory_order_relaxed) & 0x0F;
        rep[1] = uint8_t(seq << 4);
        rep[2] = 0x10;
        std::memcpy(rep.data() + 3, payload, 47);
        uint32_t crc = bt_crc32(rep.data(), 74);
        std::memcpy(rep.data() + 74, &crc, 4);
    }
    OVERLAPPED ov{};
    ov.hEvent = write_ev_;
    ResetEvent(write_ev_);
    DWORD n = 0;
    BOOL ok = WriteFile(dev_, rep.data(), DWORD(rep.size()), &n, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(write_ev_, 50) != WAIT_OBJECT_0) {
            CancelIoEx(dev_, &ov);
            GetOverlappedResult(dev_, &ov, &n, TRUE);
            return false;
        }
        ok = GetOverlappedResult(dev_, &ov, &n, FALSE);
    }
    if (!ok) return false;
    if (o.has_lightbar) lightbar_released_.store(true, std::memory_order_release);
    if (routing_ >= 0) {
        const int r = routing_;
        routed_.store(r, std::memory_order_release);
        LOGI("controller audio: %s", r == 1 ? "headphone jack" : r == 2 ? "built-in speaker" : "built-in speaker muted");
    }
    last_payload_.assign(payload, payload + 47);
    return true;
}

// Haptics over Bluetooth (format worked out by SAxense, https://apps.sdore.me/SAxense): report 0x32 = a header byte,
// a 0x11 block (7 bytes, the last a running counter) and a 0x12 block with 64 bytes of 8-bit audio, CRC-32 (seed byte
// 0xA2) over the first 138 bytes, stored after them.
bool DualSense::send_haptics(const uint8_t* pcm64) {
    std::lock_guard<std::mutex> l(dev_mu_);
    if (dev_ == INVALID_HANDLE_VALUE || usb_ || !connected_) return false;
    size_t want = std::max<size_t>(out_len_, 142);
    if (hap_buf_.size() != want) hap_buf_.resize(want);
    std::fill(hap_buf_.begin(), hap_buf_.end(), 0);
    auto& rep = hap_buf_;
    rep[0] = 0x32;
    rep[2] = 0x80 | 0x11;
    rep[3] = 7;
    const uint8_t b11[7] = {0xFE, 0, 0, 0, 0, 0xFF, hap_counter_++};
    std::memcpy(rep.data() + 4, b11, 7);
    rep[11] = 0x80 | 0x12;
    rep[12] = 64;
    std::memcpy(rep.data() + 13, pcm64, 64);
    uint32_t crc = bt_crc32(rep.data(), 138);
    std::memcpy(rep.data() + 138, &crc, 4);
    OVERLAPPED ov{};
    ov.hEvent = hap_ev_;
    ResetEvent(hap_ev_);
    DWORD n = 0;
    BOOL ok = WriteFile(dev_, rep.data(), DWORD(rep.size()), &n, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(hap_ev_, 30) != WAIT_OBJECT_0) {
            CancelIoEx(dev_, &ov);
            GetOverlappedResult(dev_, &ov, &n, TRUE);     // the cancel must finish before ov goes away
            return false;
        }
        ok = GetOverlappedResult(dev_, &ov, &n, FALSE);
    }
    return ok != FALSE;
}

void DualSense::loop() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    std::vector<uint8_t> buf(128);
    while (running_) {
        if (!open()) {
            for (int i = 0; i < 20 && running_; ++i) Sleep(100);
            continue;
        }
        buf.resize(std::max<size_t>(in_len_, 128));
        connected_ = true;
        int count = 0;
        double rate_t = now_s();
        bool pending = false;
        OVERLAPPED ov{};
        while (running_) {
            if (!pending) {
                ov = OVERLAPPED{};
                ov.hEvent = read_ev_;
                ResetEvent(read_ev_);
                DWORD n = 0;
                if (!ReadFile(dev_, buf.data(), DWORD(std::max<size_t>(in_len_, 78)), &n, &ov) &&
                    GetLastError() != ERROR_IO_PENDING) {
                    LOGW("controller read failed (%lu) - reconnecting", GetLastError());
                    break;
                }
                pending = true;
            }
            DWORD w = WaitForSingleObject(read_ev_, 10);
            InputState s = last_;
            s.t = now_s();
            s.fresh = false;
            if (w == WAIT_OBJECT_0) {
                DWORD n = 0;
                pending = false;
                if (!GetOverlappedResult(dev_, &ov, &n, FALSE)) {
                    LOGW("controller disconnected");
                    break;
                }
                if (buf[0] == 0x01 && n >= 64) {
                    parse(buf.data() + 1, s);
                    headphones_ = s.headphones;
                    s.fresh = true;
                } else if (buf[0] == 0x31 && n >= 64) {
                    parse(buf.data() + 2, s);
                    s.fresh = true;
                } else {
                    continue;
                }
                if (gyro_cal_.valid) {
                    for (int a = 0; a < 3; ++a) {
                        float v = (float(s.gyro[a]) - gyro_cal_.bias[a]) * gyro_cal_.scale[a];
                        s.gyro[a] = int16_t(clampv(std::lround(v), -32768L, 32767L));
                    }
                }
                last_ = s;
                ++count;
            }
            double t = now_s();
            if (t - rate_t >= 1.0) {
                rate_ = float(count / (t - rate_t));
                count = 0;
                rate_t = t;
            }
            OutputState out;
            try {
                if (handler_ && handler_(s, out)) write(out, false);
            } catch (const std::exception& e) {
                LOGE("controller handler: %s", e.what());
            }
        }
        if (pending && dev_ != INVALID_HANDLE_VALUE) {
            DWORD n = 0;
            CancelIoEx(dev_, &ov);
            GetOverlappedResult(dev_, &ov, &n, TRUE);
        }
        close();
    }
}

}  // namespace dsr2
