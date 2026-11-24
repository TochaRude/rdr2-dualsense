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

static std::atomic<bool> g_started{false};
static std::atomic<int> g_script_starts{0};
static std::atomic<bool> g_script_reloaded{false};
static HANDLE g_boot_thread = nullptr;

static DWORD WINAPI boot_thread(LPVOID) {
    try {
        dsr2::core().init(true);
        g_started.store(true, std::memory_order_release);
        if (dsr2::config().flag("General", "Probes", false)) dsr2::io_probe_start();
        dsr2::text_inject_start();
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

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        // Pin ourselves for process lifetime so ScriptHookRDR2 V2 FreeLibrary reloads never unmap
        // active XInput hooks, injected string-table nodes, or background worker threads.
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&DllMain), &pinned);
        dsr2::log::open((dsr2::module_dir() / "DualSenseRDR2.log").wstring());
        LOGI("DualSense for RDR2 %s - loading", dsr2::kVersion);
        g_boot_thread = CreateThread(nullptr, 0, boot_thread, nullptr, 0, nullptr);
        if (!g_boot_thread) LOGE("boot thread creation failed (%lu)", GetLastError());
    } else if (reason == DLL_PROCESS_DETACH) {
        if (!lpReserved) {
            bool boot_done = !g_boot_thread || WaitForSingleObject(g_boot_thread, 0) == WAIT_OBJECT_0;
            if (g_boot_thread) {
                CloseHandle(g_boot_thread);
                g_boot_thread = nullptr;
            }
            if (boot_done) {
                if (g_started.load(std::memory_order_acquire)) {
                    dsr2::text_inject_stop();
                    dsr2::io_probe_stop();
                    dsr2::core().shutdown();
                }
                dsr2::log::close();
            }
        } else if (g_boot_thread) {
            CloseHandle(g_boot_thread);
            g_boot_thread = nullptr;
        }
    }
    return TRUE;
}
