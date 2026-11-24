// Real Win32 test verifying ScriptHookRDR2 V2 FreeLibrary -> LoadLibrary hot-reload:
// 1. LoadLibraryW("DualSenseRDR2.asi") loads DualSenseRDR2\DualSenseCore.dll and registers script #1.
// 2. Even after DualSenseCore spawns active worker threads, FreeLibrary(hAsi) drops DualSenseRDR2.asi refcount to 0
//    and unmaps it completely while DualSenseCore.dll remains pinned in the process.
// 3. A second LoadLibraryW("DualSenseRDR2.asi") re-runs DllMain(DLL_PROCESS_ATTACH), re-registers script #2, and
//    logs "script thread started (#2) (reloaded)" with clean process exit (no 0xc0000409 / 0x40000015).
#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

extern "C" {
void mock_shv_counts(int* reg, int* unreg);
int mock_shv_run_once(void);
}

static fs::path exe_dir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return fs::path(buf).parent_path();
}

static std::string read_all(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main() {
    const fs::path dir = exe_dir();
    const fs::path asi = dir / "DualSenseRDR2.asi";
    const fs::path ini = dir / "DualSenseRDR2.ini";
    const fs::path log = dir / "DualSenseRDR2.log";

    std::error_code ec;
    fs::remove(log, ec);
    {
        std::ofstream o(ini);
        o << "[General]\nEnabled=1\nDebug=1\n";
    }

    auto fail = [&](const char* msg) -> int {
        printf("asi_reload_test FAILED: %s\n", msg);
        return 1;
    };

    HMODULE h1 = LoadLibraryW(asi.c_str());
    if (!h1) return fail("initial LoadLibraryW(DualSenseRDR2.asi) failed");

    // Wait for boot loader thread in DualSenseRDR2.asi to map DualSenseCore.dll and finish init.
    HMODULE core = nullptr;
    for (int i = 0; i < 200; ++i) {
        core = GetModuleHandleW(L"DualSenseCore.dll");
        if (core && read_all(log).find("started (in game)") != std::string::npos) break;
        Sleep(25);
    }
    if (!core) return fail("DualSenseCore.dll was not loaded by DualSenseRDR2.asi");
    Sleep(100);  // ensure loader thread completed FreeLibraryAndExitThread and workers started

    int reg = 0, unreg = 0;
    mock_shv_counts(&reg, &unreg);
    if (reg != 1 || unreg != 0) return fail("expected reg=1 unreg=0 after initial attach");

    if (!mock_shv_run_once()) return fail("mock_shv_run_once #1 failed");
    if (read_all(log).find("script thread started (#1)") == std::string::npos)
        return fail("missing 'script thread started (#1)' in log");

    // Simulate ScriptHookRDR2 V2 save-game unload: FreeLibrary(DualSenseRDR2.asi)
    if (!FreeLibrary(h1)) return fail("FreeLibrary #1 returned FALSE");
    mock_shv_counts(&reg, &unreg);
    if (unreg != 1) return fail("expected unreg=1 after FreeLibrary #1");
    if (GetModuleHandleW(L"DualSenseRDR2.asi") != nullptr)
        return fail("DualSenseRDR2.asi stayed mapped after FreeLibrary #1 (refcount > 1!)");
    if (GetModuleHandleW(L"DualSenseCore.dll") == nullptr)
        return fail("DualSenseCore.dll should remain pinned after DualSenseRDR2.asi unload");

    // Simulate ScriptHookRDR2 V2 save-game reload: LoadLibrary(DualSenseRDR2.asi)
    HMODULE h2 = LoadLibraryW(asi.c_str());
    if (!h2) return fail("reload LoadLibraryW(DualSenseRDR2.asi) failed");
    mock_shv_counts(&reg, &unreg);
    if (reg != 2) return fail("expected reg=2 after reload LoadLibraryW (DllMain did not re-run!)");

    if (!mock_shv_run_once()) return fail("mock_shv_run_once #2 failed");
    if (read_all(log).find("script thread started (#2) (reloaded)") == std::string::npos)
        return fail("missing 'script thread started (#2) (reloaded)' in log");

    if (!FreeLibrary(h2)) return fail("FreeLibrary #2 returned FALSE");
    mock_shv_counts(&reg, &unreg);
    if (unreg != 2 || GetModuleHandleW(L"DualSenseRDR2.asi") != nullptr)
        return fail("expected clean second unload (unreg=2)");

    // Process exits directly with DualSenseCore.dll pinned and worker threads alive -> must not abort/crash.
    printf("asi_reload_test: PASSED (reg=%d, unreg=%d, core pinned=%p)\n", reg, unreg, (void*)core);
    return 0;
}
