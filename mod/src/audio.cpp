#include "audio.h"

#include <timeapi.h>

#include "dsp.h"
#include "config.h"
#include "log.h"

#include <initguid.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>

#include <complex>

namespace dsr2 {

namespace {

template <class T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

std::wstring device_name(IMMDevice* dev) {
    ComPtr<IPropertyStore> props;
    std::wstring out;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) out = v.pwszVal;
        PropVariantClear(&v);
    }
    return out;
}

std::string narrow(const std::wstring& w) {
    std::string s;
    for (wchar_t c : w) s += c < 128 ? char(c) : '?';
    return s;
}

bool contains_ci(const std::wstring& hay, const std::wstring& needle) {
    std::wstring a = hay, b = needle;
    for (auto& c : a) c = towlower(c);
    for (auto& c : b) c = towlower(c);
    return a.find(b) != std::wstring::npos;
}

// the bus the endpoint's device hangs on: "BTHENUM" / "BTHLEDEVICE" = Bluetooth, "USB", "HDAUDIO" (HDMI / jack)...
std::wstring device_bus(IMMDevice* dev) {
    static const PROPERTYKEY kEnumerator = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 24};
    ComPtr<IPropertyStore> props;
    std::wstring out;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(props->GetValue(kEnumerator, &v)) && v.vt == VT_LPWSTR) out = v.pwszVal;
        PropVariantClear(&v);
    }
    return out;
}

// Device instance ID (e.g. "SWD\MMDEVAPI\..." or parent USB VID/PID "VID_054C&PID_0CE6")
std::wstring device_instance_id(IMMDevice* dev) {
    // PKEY_Device_InstanceId {78c34fc8-104a-4aca-9ea4-524d52996e57}, 256
    static const PROPERTYKEY kInstanceId = {{0x78c34fc8, 0x104a, 0x4aca, {0x9e, 0xa4, 0x52, 0x4d, 0x52, 0x99, 0x6e, 0x57}}, 256};
    ComPtr<IPropertyStore> props;
    std::wstring out;
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        if (SUCCEEDED(props->GetValue(kInstanceId, &v)) && v.vt == VT_LPWSTR) out = v.pwszVal;
        PropVariantClear(&v);
    }
    return out;
}

bool is_dualsense_endpoint(IMMDevice* dev) {
    std::wstring id = device_instance_id(dev);
    if (contains_ci(id, L"VID_054C&PID_0CE6") || contains_ci(id, L"VID_054C&PID_0DF2")) return true;
    return contains_ci(device_name(dev), L"DualSense");
}

// endpoint whose name contains `match` (and not `exclude`); empty match = default endpoint.
// When `match` is L"DualSense", also matches by Sony DualSense USB VID/PID even if renamed.
IMMDevice* find_endpoint(EDataFlow flow, const std::wstring& match, const std::wstring& exclude) {
    ComPtr<IMMDeviceEnumerator> en;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en)))
        return nullptr;
    auto excluded = [&](IMMDevice* d) {
        if (exclude.empty()) return false;
        if (contains_ci(exclude, L"DualSense") && is_dualsense_endpoint(d)) return true;
        return contains_ci(device_name(d), exclude);
    };
    if (match.empty()) {
        // the default output (multimedia, then console role), unless it is the excluded device
        for (ERole role : {eMultimedia, eConsole}) {
            IMMDevice* d = nullptr;
            if (FAILED(en->GetDefaultAudioEndpoint(flow, role, &d)) || !d) continue;
            if (!excluded(d)) return d;
            d->Release();
        }
    }
    ComPtr<IMMDeviceCollection> col;
    if (FAILED(en->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col))) return nullptr;
    UINT n = 0;
    col->GetCount(&n);
    const bool want_ds = contains_ci(match, L"DualSense");
    for (UINT i = 0; i < n; ++i) {
        IMMDevice* d = nullptr;
        if (FAILED(col->Item(i, &d))) continue;
        bool matched = match.empty() || (want_ds ? is_dualsense_endpoint(d) : contains_ci(device_name(d), match));
        if (matched && !excluded(d)) return d;
        d->Release();
    }
    return nullptr;
}

bool is_float(const WAVEFORMATEX* f) {
    // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT {00000003-0000-0010-8000-00AA00389B71}
    static const GUID kFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};
    if (f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
        return IsEqualGUID(reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f)->SubFormat, kFloat);
    return false;
}

float sample_at(const BYTE* data, const WAVEFORMATEX* f, size_t frame, int ch) {
    if (is_float(f)) return reinterpret_cast<const float*>(data)[frame * f->nChannels + ch];
    if (f->wBitsPerSample == 16) return reinterpret_cast<const int16_t*>(data)[frame * f->nChannels + ch] / 32768.0f;
    if (f->wBitsPerSample == 24) {
        const BYTE* p = data + (frame * f->nChannels + size_t(ch)) * 3;
        int32_t v = int32_t(p[0]) | (int32_t(p[1]) << 8) | (int32_t(int8_t(p[2])) << 16);
        return v / 8388608.0f;
    }
    if (f->wBitsPerSample == 32) return float(reinterpret_cast<const int32_t*>(data)[frame * f->nChannels + ch] / 2147483648.0);
    return 0.0f;
}

}  // namespace

// Where the game's sound goes now (the default output) and whether it is Bluetooth (the sound then reaches the ears
// ~150-250 ms after the PC plays it; our haptics go over USB in ~10 ms). Any thread (COM initialised here).
bool default_output(std::string& name, bool& bluetooth) {
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    IMMDevice* d = find_endpoint(eRender, L"", L"DualSense");
    bool ok = d != nullptr;
    if (d) {
        name = narrow(device_name(d));
        std::wstring bus = device_bus(d);
        bluetooth = contains_ci(bus, L"BTH") || contains_ci(bus, L"BLUETOOTH");
        d->Release();
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return ok;
}

// every output: name, bus, what Windows reports as its latency (test tool: dsr2_test --outputs)
std::string describe_outputs() {
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::string out;
    {
        ComPtr<IMMDeviceEnumerator> en;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en))) {
            std::wstring def;
            IMMDevice* dd = nullptr;
            if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dd)) && dd) { def = device_name(dd); dd->Release(); }
            ComPtr<IMMDeviceCollection> col;
            UINT n = 0;
            if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col))) col->GetCount(&n);
            for (UINT i = 0; i < n; ++i) {
                IMMDevice* d = nullptr;
                if (FAILED(col->Item(i, &d))) continue;
                std::wstring nm = device_name(d);
                ComPtr<IAudioClient> cl;
                WAVEFORMATEX* fmt = nullptr;
                REFERENCE_TIME lat = 0, per_def = 0, per_min = 0;
                UINT32 frames = 0;
                if (SUCCEEDED(d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&cl)) && SUCCEEDED(cl->GetMixFormat(&fmt)) &&
                    SUCCEEDED(cl->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 0, 0, fmt, nullptr))) {
                    cl->GetStreamLatency(&lat);
                    cl->GetDevicePeriod(&per_def, &per_min);
                    cl->GetBufferSize(&frames);
                }
                char line[512];
                snprintf(line, sizeof line, "%s %-50s bus %-12s stream latency %5.1f ms, period %4.1f ms, buffer %5.1f ms\n",
                         nm == def ? "*" : " ", narrow(nm).c_str(), narrow(device_bus(d)).c_str(), lat / 1e4, per_def / 1e4,
                         fmt && fmt->nSamplesPerSec ? 1000.0 * frames / fmt->nSamplesPerSec : 0.0);
                out += line;
                if (fmt) CoTaskMemFree(fmt);
                d->Release();
            }
        }
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return out;
}

bool AudioEngine::start(HapticLibrary* lib) {
    std::lock_guard<std::mutex> l(restart_mu_);
    lib_ = lib;
    ring_.assign(size_t(dsp::kSR) * 2, 0.0f);     // 0.5 s stereo
    ring_r_.store(0, std::memory_order_relaxed);
    ring_w_.store(0, std::memory_order_relaxed);
    mix_voices_.reserve(64);
    running_ = true;
    render_ = std::thread([this] { render_thread(); });
    return true;
}

void AudioEngine::stop() {
    running_ = false;
    ga_running_ = false;
    mic_running_ = false;
    for (auto* t : {&render_, &loop_, &mic_})
        if (t->joinable()) t->join();
    ok_ = false;
}

void AudioEngine::restart() {
    std::lock_guard<std::mutex> l(restart_mu_);
    stop();
    {
        std::lock_guard<std::mutex> ml(mu_);
        incoming_.clear();
        mix_voices_.clear();
        env_voices_.clear();
    }
    ring_r_.store(0, std::memory_order_relaxed);
    ring_w_.store(0, std::memory_order_relaxed);
    purge_speaker_.store(false, std::memory_order_relaxed);
    running_ = true;
    render_ = std::thread([this] { render_thread(); });
    if (ga_started_) {
        ga_running_ = true;
        loop_ = std::thread([this] { loopback_thread(ga_hint_); });
    }
    if (mic_started_) {
        mic_running_ = true;
        mic_ = std::thread([this] { mic_thread(mic_sens_.load()); });
    }
    LOGI("audio devices reopened");
}

void AudioEngine::set_speaker_volume(float v) {
    speaker_vol_ = v;
    if (v <= 0.001f) {
        {
            std::lock_guard<std::mutex> l(mu_);
            incoming_.erase(std::remove_if(incoming_.begin(), incoming_.end(), [](const Voice& x) { return x.speaker; }),
                            incoming_.end());
        }
        purge_speaker_.store(true, std::memory_order_release);
    }
}

void AudioEngine::play(const Cue& c) {
    Stereo buf;
    Cue cue = c;
    cue.gain *= strength_.load();
    if (!lib_ || !lib_->render(cue, buf)) return;
    const bool live = ok_.load();
    EnvVoice env_v;
    if (!live) {
        // Bluetooth rumble fallback: compute envelope outside mu_ so we never stall the render thread
        env_v.t0 = now_s();
        const size_t hop = size_t(dsp::kSR * 0.004) * 2;
        env_v.env.reserve(buf.size() / hop + 1);
        for (size_t i = 0; i < buf.size(); i += hop) {
            float l = 0, r = 0;
            for (size_t k = i; k < std::min(buf.size(), i + hop); k += 2) {
                l = std::max(l, std::fabs(buf[k]));
                r = std::max(r, std::fabs(buf[k + 1]));
            }
            env_v.env.push_back({l, r});
        }
    }
    Stereo spk;
    bool has_spk = false;
    if (c.speaker && live && !bt_active_.load() && speaker_vol_.load() > 0.001f)
        has_spk = lib_->speaker_sound(c, spk);
    std::lock_guard<std::mutex> l(mu_);
    if (live) incoming_.push_back({std::move(buf), 0, false, c.scripted});
    else env_voices_.push_back(std::move(env_v));
    if (has_spk) incoming_.push_back({std::move(spk), 0, true, false});
}

void AudioEngine::track_play(uint64_t key, std::shared_ptr<const std::vector<float>> mono, double t0, float gl, float gr) {
    std::lock_guard<std::mutex> l(mu_);
    Track& t = tracks_[key];
    t.mono = std::move(mono);
    t.t0 = t0;
    t.pos_init = false;
    t.gl = gl;
    t.gr = gr;
    t.stopping = false;
}

void AudioEngine::track_move(uint64_t key, double t0, float gl, float gr) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = tracks_.find(key);
    if (it == tracks_.end()) return;
    if (std::fabs(it->second.t0 - t0) > 0.02) it->second.pos_init = false;
    it->second.t0 = t0;
    it->second.gl = gl;
    it->second.gr = gr;
}

void AudioEngine::track_stop(uint64_t key) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = tracks_.find(key);
    if (it != tracks_.end()) it->second.stopping = true;
}

bool AudioEngine::track_playing(uint64_t key) {
    std::lock_guard<std::mutex> l(mu_);
    auto it = tracks_.find(key);
    return it != tracks_.end() && !it->second.stopping;
}

int AudioEngine::track_count() {
    std::lock_guard<std::mutex> l(mu_);
    return int(tracks_.size());
}

void AudioEngine::rumble_tick(double now, uint8_t& left, uint8_t& right) {
    float l = 0, r = 0;
    std::lock_guard<std::mutex> lk(mu_);
    while (!env_voices_.empty() && now >= env_voices_.front().t0 &&
           (now - env_voices_.front().t0) / 0.004 >= env_voices_.front().env.size())
        env_voices_.pop_front();
    for (auto& v : env_voices_) {
        if (now < v.t0) continue;
        size_t i = size_t((now - v.t0) / 0.004);
        if (i < v.env.size()) {
            l = std::max(l, v.env[i].first);
            r = std::max(r, v.env[i].second);
        }
    }
    float g = rumble_gain_.load();
    l = std::max(l, rumble_low_.load() * g);
    r = std::max(r, rumble_high_.load() * g);
    left = uint8_t(std::min(255.0f, l * 255));
    right = uint8_t(std::min(255.0f, r * 200));
}

void AudioEngine::render_thread() {
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool fallback_bt = false;
    {
        IMMDevice* dev = find_endpoint(eRender, L"DualSense", L"");
        if (!dev) {
            fallback_bt = true;
        } else {
            LOGI("haptics output: %s", narrow(device_name(dev)).c_str());
            ComPtr<IAudioClient> client;
            ComPtr<IAudioRenderClient> render;
            WAVEFORMATEX* fmt = nullptr;
            HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            bool good = SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client)) &&
                        SUCCEEDED(client->GetMixFormat(&fmt)) && fmt->nChannels >= 4 &&
                        SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 100000, 0, fmt, nullptr)) &&
                        SUCCEEDED(client->SetEventHandle(ev)) &&
                        SUCCEEDED(client->GetService(__uuidof(IAudioRenderClient), (void**)&render));
            dev->Release();
            if (!good) {
                LOGE("could not open the DualSense audio device (channels %d)", fmt ? fmt->nChannels : 0);
                if (fmt) CoTaskMemFree(fmt);
                if (ev) CloseHandle(ev);
            } else {
                UINT32 frames = 0;
                client->GetBufferSize(&frames);
                REFERENCE_TIME lat = 0;
                client->GetStreamLatency(&lat);
                latency_ms_ = float(lat) / 10000.0f + 1000.0f * frames / fmt->nSamplesPerSec / 2;
                const int ch = fmt->nChannels;
                const bool flt = is_float(fmt);
                const int bps = fmt->wBitsPerSample;
                if (fmt->nSamplesPerSec != dsp::kSR)
                    LOGW("haptics device runs at %lu Hz (expected 48000) - effects will play at the wrong speed", fmt->nSamplesPerSec);
                LOGI("haptics stream: %d ch, %lu Hz, %s (%d bit), buffer %u frames, latency %.1f ms", ch, fmt->nSamplesPerSec,
                     flt ? "float" : "pcm", bps, frames, latency_ms_.load());
                DWORD task = 0;
                HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
                client->Start();
                ok_ = true;
                std::vector<float> mix;
                mix.reserve(size_t(frames) * 4);
                while (running_) {
                    if (WaitForSingleObject(ev, 200) != WAIT_OBJECT_0) continue;
                    UINT32 pad = 0;
                    if (FAILED(client->GetCurrentPadding(&pad))) break;
                    UINT32 n = frames - pad;
                    if (!n) continue;
                    mix_block(mix, n, float(fmt->nSamplesPerSec));
                    BYTE* out = nullptr;
                    if (FAILED(render->GetBuffer(n, &out))) break;
                    for (UINT32 i = 0; i < n; ++i)
                        for (int c = 0; c < ch; ++c) {
                            float s = c < 4 ? clampv(mix[size_t(i) * 4 + c], -1.0f, 1.0f) : 0.0f;
                            size_t idx = size_t(i) * ch + c;
                            if (flt) reinterpret_cast<float*>(out)[idx] = s;
                            else if (bps == 16) reinterpret_cast<int16_t*>(out)[idx] = int16_t(s * 32767.0f);
                            else if (bps == 24) {
                                int32_t v = int32_t(s * 8388607.0f);
                                out[idx * 3 + 0] = BYTE(v & 0xFF);
                                out[idx * 3 + 1] = BYTE((v >> 8) & 0xFF);
                                out[idx * 3 + 2] = BYTE((v >> 16) & 0xFF);
                            } else if (bps == 32) {
                                reinterpret_cast<int32_t*>(out)[idx] = int32_t(s * 2147483647.0);
                            }
                        }
                    render->ReleaseBuffer(n, 0);
                }
                ok_ = false;
                client->Stop();
                if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
                CoTaskMemFree(fmt);
                if (ev) CloseHandle(ev);
            }
        }
    }
    if (SUCCEEDED(co)) CoUninitialize();
    if (fallback_bt) {
        if (bt_sink_) bt_loop();             // Bluetooth: haptics in the controller's HID reports (or rumble)
        else LOGW("DualSense audio device not found - haptics fall back to rumble emulation (Bluetooth?)");
    }
}

// One block of the mix (4 channels: speaker L/R, haptics L/R) at sample rate sr: the game's rumble as vibration, the
// game-audio layer, every playing effect, the tracks on the game's clock. Used by the sound device (USB) and by the
// Bluetooth loop - never both at once.
void AudioEngine::mix_block(std::vector<float>& mix, uint32_t n, float sr) {
    {
        std::lock_guard<std::mutex> l(mu_);
        for (auto& v : incoming_) mix_voices_.push_back(std::move(v));
        incoming_.clear();
    }
    if (purge_speaker_.exchange(false, std::memory_order_acquire)) {
        mix_voices_.erase(std::remove_if(mix_voices_.begin(), mix_voices_.end(), [](const Voice& v) { return v.speaker; }),
                          mix_voices_.end());
    }
    mix.assign(size_t(n) * 4, 0.0f);    // ch0/1 speaker, ch2/3 haptics
    bool scripted = false;
    for (auto& v : mix_voices_) scripted |= v.scripted && !v.speaker;
    scripted_active_ = scripted;
    float duck_target = scripted ? duck_level_.load() : 1.0f;
    float strength = strength_.load(), spk = speaker_vol_.load();
    float rg = rumble_gain_.load() * strength, ga = game_audio_gain_.load() * strength;
    float tl = rumble_low_.load(), th = rumble_high_.load();
    // Strict SPSC consumer: drop stale backlog (> 40 ms) from the consumer side only
    size_t r = ring_r_.load(std::memory_order_relaxed);
    size_t w = ring_w_.load(std::memory_order_acquire);
    const size_t rsz = ring_.size();
    if (rsz > 0) {
        size_t fill = (w + rsz - r) % rsz;
        const size_t max_fill = size_t(dsp::kSR * 0.04) * 2;
        if (fill > max_fill) r = (w + rsz - size_t(dsp::kSR * 0.015) * 2) % rsz;
    }
    for (uint32_t i = 0; i < n; ++i) {
        duck_ += (duck_target - duck_) * (duck_target < duck_ ? 0.01f : 0.0003f);
        // the game's rumble motors as voice-coil vibration (low ~45 Hz, high ~150 Hz)
        rl_ += (tl - rl_) * 0.002f;
        rh_ += (th - rh_) * 0.002f;
        ph_l_ += 2 * kPi * 45.0f / sr;
        ph_h_ += 2 * kPi * 150.0f / sr;
        if (ph_l_ > 2 * kPi) ph_l_ -= 2 * kPi;
        if (ph_h_ > 2 * kPi) ph_h_ -= 2 * kPi;
        float rum = (std::sin(ph_l_) * rl_ * 0.8f + std::sin(ph_h_) * rh_ * 0.5f) * rg * duck_;
        float hl = rum, hr = rum;
        // game-audio layer
        if (r != w) {
            hl += ring_[r] * ga * duck_;
            hr += ring_[r + 1] * ga * duck_;
            r = (r + 2) % rsz;
        }
        mix[size_t(i) * 4 + 2] = hl;
        mix[size_t(i) * 4 + 3] = hr;
    }
    ring_r_.store(r, std::memory_order_release);
    // voices are 48 kHz; the device is normally 48 kHz too (ratio 1)
    for (auto& v : mix_voices_) {
        size_t frames_v = v.buf.size() / 2;
        for (uint32_t i = 0; i < n && v.pos < frames_v; ++i) {
            size_t k = v.pos;
            if (v.speaker) {
                // mono: the built-in speaker only plays the right channel (headphones get both)
                float m = 0.5f * (v.buf[2 * k] + v.buf[2 * k + 1]) * spk;
                mix[size_t(i) * 4 + 0] += m;
                mix[size_t(i) * 4 + 1] += m;
            } else {
                mix[size_t(i) * 4 + 2] += v.buf[2 * k];
                mix[size_t(i) * 4 + 3] += v.buf[2 * k + 1];
            }
            v.pos += 1;
        }
    }
    mix_voices_.erase(std::remove_if(mix_voices_.begin(), mix_voices_.end(), [](const Voice& v) { return v.pos >= v.buf.size() / 2; }),
                      mix_voices_.end());
    // tracks on the game's clock: continuous sample cursor to avoid QPC jitter clicks; resync only on large drift
    {
        std::lock_guard<std::mutex> l(mu_);
        const double felt = now_s() + latency_ms_.load() / 1000.0;
        const float step = 1.0f / (0.06f * float(dsp::kSR));          // gains move / fade over ~60 ms
        const bool muted = tracks_muted_.load();                        // the game is paused
        for (auto it = tracks_.begin(); it != tracks_.end();) {
            Track& t = it->second;
            const std::vector<float>& m = *t.mono;
            const long long k_target = (long long)std::floor((felt - t.t0) * dsp::kSR);
            if (!t.pos_init || std::llabs(t.pos - k_target) > 400) {    // > ~8.3 ms drift or first block
                t.pos = k_target;
                t.pos_init = true;
            }
            const long long k0 = t.pos;
            t.pos += n;
            const bool silent = t.stopping || muted;
            float tl = silent ? 0.0f : t.gl * strength, tr = silent ? 0.0f : t.gr * strength;
            for (uint32_t i = 0; i < n; ++i) {
                t.cl += clampv(tl - t.cl, -step, step);
                t.cr += clampv(tr - t.cr, -step, step);
                long long k = k0 + i;
                if (k < 0 || k >= (long long)m.size()) continue;
                mix[size_t(i) * 4 + 2] += m[size_t(k)] * t.cl;
                mix[size_t(i) * 4 + 3] += m[size_t(k)] * t.cr;
            }
            bool done = (t.stopping && t.cl <= 0 && t.cr <= 0) || k0 > (long long)m.size();
            it = done ? tracks_.erase(it) : std::next(it);
        }
    }
}

void AudioEngine::bt_loop() {
    std::vector<float> mix;
    mix.reserve(512 * 4);
    const double period = 64.0 / 6000.0;                 // 32 frames at 3 kHz
    double next = now_s(), fail_log_t = 0;
    bool active = false, warned = false, timer_hi = false;
    int fails = 0;
    while (running_) {
        const bool ready = bt_on_.load() && bt_ready_ && bt_ready_();
        if (ready != active) {
            active = ready;
            bt_active_ = ready;
            latency_ms_ = ready ? 30.0f : 0.0f;          // one report + the Bluetooth link (estimate)
            ok_ = ready;
            next = now_s();
            if (ready && !timer_hi) { timeBeginPeriod(1); timer_hi = true; }
            else if (!ready && timer_hi) { timeEndPeriod(1); timer_hi = false; }
            if (ready) LOGI("haptics output: DualSense over Bluetooth (audio in HID reports, %s%s)",
                            bt_u8_.load() ? "unsigned" : "signed", bt_swap_.load() ? ", channels swapped" : "");
        }
        if (!ready) {
            if (!warned) LOGW("DualSense audio device not found - haptics fall back to rumble emulation (Bluetooth?)");
            warned = true;
            Sleep(100);
            continue;
        }
        warned = false;
        const double now = now_s();
        if (now < next) {
            Sleep(1);
            continue;
        }
        if (now - next > 0.1) next = now;                // far behind (a hitch): don't send a burst
        next += period;
        mix_block(mix, 512, float(dsp::kSR));
        uint8_t pcm[64];
        const bool u8 = bt_u8_.load(), swap = bt_swap_.load();
        for (int f = 0; f < 32; ++f) {
            float l = 0, r = 0;
            for (int k = 0; k < 16; ++k) {
                l += mix[size_t(f * 16 + k) * 4 + 2];
                r += mix[size_t(f * 16 + k) * 4 + 3];
            }
            int a = int(std::lround(clampv(l / 16, -1.0f, 1.0f) * 127.0f));
            int b = int(std::lround(clampv(r / 16, -1.0f, 1.0f) * 127.0f));
            if (swap) std::swap(a, b);
            pcm[2 * f] = u8 ? uint8_t(a + 128) : uint8_t(int8_t(a));
            pcm[2 * f + 1] = u8 ? uint8_t(b + 128) : uint8_t(int8_t(b));
        }
        if (!bt_sink_(pcm)) {
            ++fails;
            if (now - fail_log_t > 5.0) {
                LOGW("haptics: %d Bluetooth audio reports failed to send", fails);
                fail_log_t = now;
                fails = 0;
            }
        }
    }
    bt_active_ = false;
    ok_ = false;
    if (timer_hi) timeEndPeriod(1);
}

void AudioEngine::start_game_audio(float gain, const std::string& hint) {
    std::lock_guard<std::mutex> l(restart_mu_);
    game_audio_gain_ = gain;
    if (loop_.joinable() && !ga_running_.load()) loop_.join();
    if (gain <= 0 || loop_.joinable()) return;
    ga_started_ = true;
    ga_hint_ = hint;
    ga_running_ = true;
    loop_ = std::thread([this, hint] { loopback_thread(hint); });
}

void AudioEngine::loopback_thread(std::string hint) {
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        std::wstring whint(hint.begin(), hint.end());
        IMMDevice* dev = find_endpoint(eRender, whint, L"");
        if (!dev) {
            LOGW("game-audio layer: no audio output to listen to");
        } else {
            const bool controller = is_dualsense_endpoint(dev);
            LOGI("game-audio layer listens to: %s%s", narrow(device_name(dev)).c_str(),
                 controller ? " (sound channels only)" : "");
            ComPtr<IAudioClient> client;
            ComPtr<IAudioCaptureClient> cap;
            WAVEFORMATEX* fmt = nullptr;
            bool good = SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client)) &&
                        SUCCEEDED(client->GetMixFormat(&fmt)) &&
                        SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 200000, 0, fmt, nullptr)) &&
                        SUCCEEDED(client->GetService(__uuidof(IAudioCaptureClient), (void**)&cap));
            dev->Release();
            if (!good) {
                LOGE("game-audio layer: loopback capture failed");
                if (fmt) CoTaskMemFree(fmt);
            } else {
                const int ch = fmt->nChannels;
                const double step = double(fmt->nSamplesPerSec) / dsp::kSR;
                LOGI("game-audio layer: %d ch %lu Hz", ch, fmt->nSamplesPerSec);
                dsp::StreamConverter conv;
                conv.gain = 1.0f;
                client->Start();
                double rs_pos = 0;
                float prev_l = 0, prev_r = 0;
                const size_t rsz = ring_.size();
                while (running_) {
                    Sleep(4);
                    UINT32 packet = 0;
                    HRESULT hr = cap->GetNextPacketSize(&packet);
                    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                        LOGW("game-audio layer: output device disconnected");
                        break;
                    }
                    while (SUCCEEDED(hr) && packet) {
                        BYTE* data = nullptr;
                        UINT32 n = 0;
                        DWORD flags = 0;
                        if (FAILED(cap->GetBuffer(&data, &n, &flags, nullptr, nullptr))) break;
                        bool silent = flags & AUDCLNT_BUFFERFLAGS_SILENT;
                        size_t w = ring_w_.load(std::memory_order_relaxed);
                        const size_t r = ring_r_.load(std::memory_order_acquire);
                        for (UINT32 i = 0; i < n; ++i) {
                            float l = 0, r_ch = 0;
                            if (!silent) {
                                l = sample_at(data, fmt, i, 0);
                                r_ch = ch > 1 ? sample_at(data, fmt, i, 1) : l;
                                if (ch >= 6 && !controller) {
                                    float c = sample_at(data, fmt, i, 2), lfe = sample_at(data, fmt, i, 3);
                                    l += 0.7f * c + 0.7f * lfe + 0.6f * sample_at(data, fmt, i, 4);
                                    r_ch += 0.7f * c + 0.7f * lfe + 0.6f * sample_at(data, fmt, i, 5);
                                }
                            }
                            while (rs_pos <= 1.0) {
                                float sl = prev_l + (l - prev_l) * float(rs_pos), sr_s = prev_r + (r_ch - prev_r) * float(rs_pos);
                                float hl, hr_s;
                                conv.process(sl, sr_s, hl, hr_s);
                                size_t nw = (w + 2) % rsz;
                                if (nw != r) {          // strict SPSC: drop new sample if full, never write ring_r_
                                    ring_[w] = hl;
                                    ring_[w + 1] = hr_s;
                                    w = nw;
                                }
                                rs_pos += step;
                            }
                            rs_pos -= 1.0;
                            prev_l = l;
                            prev_r = r_ch;
                        }
                        ring_w_.store(w, std::memory_order_release);
                        cap->ReleaseBuffer(n);
                        hr = cap->GetNextPacketSize(&packet);
                    }
                    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                        LOGW("game-audio layer: output device disconnected");
                        break;
                    }
                }
                client->Stop();
                CoTaskMemFree(fmt);
            }
        }
    }
    if (SUCCEEDED(co)) CoUninitialize();
    ga_running_ = false;
}

void AudioEngine::start_mic(float sens, std::function<void()> on_whistle) {
    std::lock_guard<std::mutex> l(restart_mu_);
    {
        std::lock_guard<std::mutex> ml(mu_);
        on_whistle_ = std::move(on_whistle);
    }
    cooldown_s_ = std::max(0.3f, config().num("Mic", "Cooldown", 1.2f));
    mic_sens_ = sens;
    if (mic_.joinable() && !mic_running_.load()) mic_.join();
    if (mic_.joinable()) return;
    mic_started_ = true;
    mic_running_ = true;
    mic_ = std::thread([this] { mic_thread(mic_sens_.load()); });
}

void AudioEngine::stop_mic() {
    std::lock_guard<std::mutex> l(restart_mu_);
    mic_started_ = false;
    mic_running_ = false;
    if (mic_.joinable()) mic_.join();
    mic_level_ = 0.0f;
}

// A whistle is a strong pure tone (500-4000 Hz) far above the rest of the spectrum, held ~0.15 s.
void AudioEngine::mic_thread(float sens) {
    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    {
        IMMDevice* dev = find_endpoint(eCapture, L"DualSense", L"");
        if (!dev) {
            LOGW("whistle: DualSense microphone not found");
        } else {
            ComPtr<IAudioClient> client;
            ComPtr<IAudioCaptureClient> cap;
            WAVEFORMATEX* fmt = nullptr;
            bool good = SUCCEEDED(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&client)) &&
                        SUCCEEDED(client->GetMixFormat(&fmt)) &&
                        SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 200000, 0, fmt, nullptr)) &&
                        SUCCEEDED(client->GetService(__uuidof(IAudioCaptureClient), (void**)&cap));
            dev->Release();
            if (!good) {
                LOGE("whistle: could not open the microphone");
                if (fmt) CoTaskMemFree(fmt);
            } else {
                LOGI("whistle detector on the controller mic (%d ch %lu Hz)", fmt->nChannels, fmt->nSamplesPerSec);
                const int N = 1024;
                const float sr = float(fmt->nSamplesPerSec);
                std::vector<float> hann(N);
                for (int k = 0; k < N; ++k) hann[k] = 0.5f - 0.5f * std::cos(2 * kPi * k / (N - 1));
                std::vector<float> frame;
                frame.reserve(N);
                std::vector<std::complex<float>> spec(N);
                int run = 0, misses = 0;
                float low_bg = 0;                  // background energy 80-400 Hz (game sound, room)
                float last_hz = 0;
                double cooldown_until = 0;
                const int max_bin = N / 2 - 1;
                const int band_lo = clampv(int(250 * N / sr), 1, max_bin);
                const int band_hi = clampv(int(6000 * N / sr), band_lo + 1, N / 2);
                const int lo = clampv(int(850 * N / sr), band_lo, band_hi - 1);
                const int hi = clampv(int(4000 * N / sr), lo, band_hi - 1);
                const int low_lo = clampv(int(80 * N / sr), 1, max_bin);
                const int low_hi = clampv(int(400 * N / sr), low_lo + 1, N / 2);
                std::vector<float> mags(size_t(band_hi - band_lo));
                client->Start();
                while (running_ && mic_running_) {
                    Sleep(10);
                    UINT32 packet = 0;
                    HRESULT hr = cap->GetNextPacketSize(&packet);
                    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                        LOGW("whistle: microphone disconnected");
                        break;
                    }
                    while (SUCCEEDED(hr) && packet) {
                        BYTE* data = nullptr;
                        UINT32 n = 0;
                        DWORD flags = 0;
                        if (FAILED(cap->GetBuffer(&data, &n, &flags, nullptr, nullptr))) break;
                        const bool listen = mic_listening_.load();
                        if (!listen) {
                            mic_level_ = 0.0f;
                            frame.clear();
                            run = misses = 0;
                            cap->ReleaseBuffer(n);
                            hr = cap->GetNextPacketSize(&packet);
                            continue;
                        }
                        for (UINT32 i = 0; i < n; ++i) {
                            float s = 0;
                            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                                for (int c = 0; c < fmt->nChannels; ++c) s += sample_at(data, fmt, i, c);
                                s /= fmt->nChannels;
                            }
                            frame.push_back(s);
                            if (int(frame.size()) < N) continue;
                            {
                                float pk = 0;
                                for (float v : frame) pk = std::max(pk, std::fabs(v));
                                mic_level_ = pk;
                            }
                            for (int k = 0; k < N; ++k) spec[k] = {frame[k] * hann[k], 0.0f};
                            frame.erase(frame.begin(), frame.begin() + N / 2);     // 50 % overlap
                            dsp::fft(spec);
                            int best = lo;
                            float best_v = 0, total = 0;
                            for (int k = band_lo; k < band_hi; ++k) {
                                float v = std::abs(spec[k]);
                                mags[size_t(k - band_lo)] = v;
                                total += v * v;
                                if (k >= lo && k <= hi && v > best_v) { best_v = v; best = k; }
                            }
                            float peak_e = 0;
                            for (int k = std::max(band_lo, best - 2); k <= std::min(band_hi - 1, best + 2); ++k)
                                peak_e += std::norm(spec[k]);
                            float purity = peak_e / std::max(1e-12f, total);
                            float low_e = 0;
                            for (int k = low_lo; k < low_hi; ++k) low_e += std::norm(spec[k]);
                            bool voiced = low_e > 4.0f * low_bg && low_e > 0.3f * peak_e;
                            low_bg = low_bg <= 0 ? low_e : low_bg + (std::min(low_e, 3.0f * low_bg) - low_bg) * 0.02f;
                            std::nth_element(mags.begin(), mags.begin() + mags.size() / 2, mags.end());
                            float median = std::max(1e-7f, mags[mags.size() / 2]);
                            float hz = best * sr / N;
                            float level = best_v / (N / 4);
                            float s_eff = std::max(0.3f, mic_sens_.load());
                            bool tonal = best_v > median * (12.0f / s_eff) && level > 0.0025f / s_eff && purity > 0.55f / std::sqrt(s_eff) && !voiced;
                            bool steady = last_hz <= 0 || std::fabs(hz - last_hz) / last_hz < 0.3f;
                            if (tonal && steady) {
                                ++run;
                                misses = 0;
                                last_hz = hz;
                            } else if (run > 0 && ++misses <= 2) {
                            } else {
                                if (run >= 3 && run < int(0.15f * sr / (N / 2)))
                                    LOGD("whistle candidate too short (%.0f Hz, %d frames)", last_hz, run);
                                run = 0;
                                misses = 0;
                                last_hz = 0;
                            }
                            double t = now_s();
                            float cd = cooldown_s_.load();
                            if (run == int(0.15f * sr / (N / 2)) && t <= cooldown_until)
                                LOGD("whistle heard again during the %.1f s cooldown (%.0f Hz)", cd, hz);
                            if (run >= int(0.15f * sr / (N / 2)) && t > cooldown_until) {
                                cooldown_until = t + cd;
                                run = 0;
                                LOGI("whistle detected (%.0f Hz)", hz);
                                std::function<void()> cb;
                                {
                                    std::lock_guard<std::mutex> ml(mu_);
                                    cb = on_whistle_;
                                }
                                if (cb) cb();
                            }
                        }
                        cap->ReleaseBuffer(n);
                        hr = cap->GetNextPacketSize(&packet);
                    }
                    if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                        LOGW("whistle: microphone disconnected");
                        break;
                    }
                }
                client->Stop();
                CoTaskMemFree(fmt);
            }
        }
    }
    if (SUCCEEDED(co)) CoUninitialize();
    mic_running_ = false;
}

}  // namespace dsr2
