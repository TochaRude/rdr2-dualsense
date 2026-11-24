// DualSenseRDR2.asi - loaded by the ASI loader, runs a ScriptHookRDR2 script for game state.
#include "config.h"
#include "io_probe.h"
#include "common.h"
#include "core.h"
#include "log.h"
#include "shv.h"
#include "text_inject.h"

#include <atomic>

static HMODULE g_module = nullptr;

namespace dsr2 {
fs::path module_dir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_module, path, MAX_PATH);
    return fs::path(path).parent_path();
}
}  // namespace dsr2

static std::atomic<bool> g_started{false};
static std::atomic<int> g_script_starts{0};
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

static void script_main() {
    int n = ++g_script_starts;
    LOGI("script thread started (#%d)", n);
    while (true) {
        try {
            dsr2::core().script_tick();
        } catch (const std::exception& e) {
            LOGE("script tick: %s", e.what());
        } catch (...) {
            LOGE("script tick: unknown exception");
        }
        shv_scriptWait(0);
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        dsr2::log::open((dsr2::module_dir() / "DualSenseRDR2.log").wstring());
        LOGI("DualSense for RDR2 %s - loading", dsr2::kVersion);
        shv_scriptRegister(module, script_main);
        g_boot_thread = CreateThread(nullptr, 0, boot_thread, nullptr, 0, nullptr);
        if (!g_boot_thread) LOGE("boot thread creation failed (%lu)", GetLastError());
    } else if (reason == DLL_PROCESS_DETACH) {
        // Only clean up on explicit FreeLibrary (lpReserved == nullptr) when boot_thread is not still running
        // (avoids Loader Lock deadlock on FreeLibrary). Never touch ScriptHook or locks on process exit (lpReserved != nullptr).
        if (!lpReserved) {
            shv_scriptUnregister(module);
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
