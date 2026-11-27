// DualSenseCore.dll - loaded by the thin DualSenseRDR2.asi shim and pinned for the process lifetime
// so XInput hooks, injected strings, and worker threads survive ScriptHookRDR2 V2 save-load reloads.
#include "config.h"
#include "io_probe.h"
#include "common.h"
#include "core.h"
#include "log.h"
#include "text_inject.h"
#include "world.h"

#include <atomic>

static HMODULE g_module = nullptr;

namespace dsr2 {
fs::path module_dir() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_module, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return fs::current_path();
    fs::path dir = fs::path(path).parent_path();
    std::string folder = dir.filename().string();
    for (char& c : folder)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    if (folder == "dualsenserdr2") return dir.parent_path();
    return dir;
}
}  // namespace dsr2

static std::atomic<int> g_script_starts{0};
static std::atomic<bool> g_script_reloaded{false};
static std::atomic<bool> g_core_ready{false};
static wchar_t g_log_path[MAX_PATH] = {};

static void append_detach_log(const char* msg) noexcept {
    if (!g_log_path[0] || !msg) return;
    HANDLE f = CreateFileW(g_log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD len = 0;
    while (msg[len]) ++len;
    DWORD written = 0;
    WriteFile(f, msg, len, &written, nullptr);
    CloseHandle(f);
}

// Controller, audio and the XInput hook start right away (menus work too);
// game state comes from the ScriptHook script once a save is loaded.
static DWORD WINAPI boot_thread(LPVOID) {
    try {
        (void)dsr2::core();
        g_core_ready.store(true, std::memory_order_release);
        dsr2::core().init(true);
        if (dsr2::config().flag("General", "Probes", false)) dsr2::io_probe_start();   // research: which audio files the game reads
    } catch (const std::exception& e) {
        LOGE("init failed: %s", e.what());
    } catch (...) {
        LOGE("init failed: unknown exception");
    }
    return 0;
}

extern "C" __declspec(dllexport) void dsr2_script_event(int ev) {
    if (ev == 0) {
        g_script_reloaded.store(true, std::memory_order_release);
        if (g_core_ready.load(std::memory_order_acquire)) {
            dsr2::core().request_exit_reset();
        }
        return;
    }
    if (ev == 1) {
        int n = ++g_script_starts;
        bool reloaded = g_script_reloaded.exchange(false, std::memory_order_acq_rel);
        LOGI("script thread started (#%d)%s", n, (reloaded || n > 1) ? " (reloaded)" : "");
        if (reloaded || n > 1) dsr2::world().on_script_reload();
    }
}

extern "C" __declspec(dllexport) void dsr2_script_tick(void) {
    try {
        dsr2::core().script_tick();
    } catch (const std::exception& e) {
        LOGE("script tick: %s", e.what());
    } catch (...) {
        LOGE("script tick: unknown exception");
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        // Pin ourselves for process lifetime so ScriptHookRDR2 V2 FreeLibrary reloads never unmap
        // active XInput hooks, injected string-table nodes, or background worker threads.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&DllMain), &pinned);
        const dsr2::fs::path log_p = dsr2::module_dir() / "DualSenseRDR2.log";
        const std::wstring log_w = log_p.wstring();
        const size_t n = log_w.size() < MAX_PATH - 1 ? log_w.size() : size_t(MAX_PATH - 1);
        for (size_t i = 0; i < n; ++i) g_log_path[i] = log_w[i];
        g_log_path[n] = L'\0';
        dsr2::log::open(log_w);
#ifdef DSR2_DS4_EXPERIMENTAL
        LOGI("DualShock 4 (Experimental) for RDR2 %s - loading", dsr2::kVersion);
#else
        LOGI("DualSense for RDR2 %s - loading", dsr2::kVersion);
#endif
        if (HANDLE t = CreateThread(nullptr, 0, boot_thread, nullptr, 0, nullptr)) CloseHandle(t);
        else LOGE("boot thread creation failed (%lu)", GetLastError());
        dsr2::text_inject_start();       // our Settings texts, in the game's own string table (even if init fails)
    } else if (reason == DLL_PROCESS_DETACH) {
        // Under the loader lock we must never lock mutexes or join threads, so do a lock-free final HID reset
        // and close the controller handle directly to clear lightbar, LEDs, triggers, rumble, and speaker route.
        if (g_core_ready.load(std::memory_order_acquire)) {
            const int st = dsr2::core().pad().reset_on_exit();
            append_detach_log(st > 0 ? "[INFO] exit: process detach controller reset ok\r\n"
                            : st == 0 ? "[INFO] exit: process detach (no controller handle open)\r\n"
                                      : "[WARN] exit: process detach controller reset write failed\r\n");
        }
    }
    return TRUE;
}
