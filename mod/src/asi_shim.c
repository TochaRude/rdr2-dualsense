// Thin, CRT-free ASI shim for ScriptHookRDR2 (including ScriptHookRDR2 V2 save-load hot-reloads).
// Keeps zero persistent threads and zero TLS so FreeLibrary() drops refcount to 0, while
// DualSenseRDR2\DualSenseCore.dll stays pinned with active XInput hooks and audio/HID threads.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

void shv_scriptRegister(HMODULE module, void (*main)(void));
void shv_scriptUnregister(HMODULE module);
void shv_scriptWait(DWORD ms);

typedef void (*dsr2_event_fn)(int ev);
typedef void (*dsr2_tick_fn)(void);

static PVOID volatile g_event_fn = NULL;
static PVOID volatile g_tick_fn = NULL;

typedef struct {
    HMODULE self;
    wchar_t asi_dir[MAX_PATH];
    wchar_t core_path[MAX_PATH];
} LoaderCtx;

static void wcopy(wchar_t* dst, size_t cap, const wchar_t* src) {
    size_t i = 0;
    if (!cap) return;
    while (i + 1 < cap && src[i]) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = L'\0';
}

static void wcat(wchar_t* dst, size_t cap, const wchar_t* src) {
    size_t i = 0, j = 0;
    if (!cap) return;
    while (i < cap && dst[i]) ++i;
    while (i + 1 < cap && src[j]) {
        dst[i++] = src[j++];
    }
    dst[i < cap ? i : cap - 1] = L'\0';
}

static void bind_core(HMODULE core) {
    FARPROC ev = GetProcAddress(core, "dsr2_script_event");
    FARPROC tk = GetProcAddress(core, "dsr2_script_tick");
    InterlockedExchangePointer(&g_event_fn, (PVOID)(uintptr_t)ev);
    InterlockedExchangePointer(&g_tick_fn, (PVOID)(uintptr_t)tk);
}

static void log_missing_core(const wchar_t* asi_dir) {
    wchar_t log_path[MAX_PATH];
    wcopy(log_path, MAX_PATH, asi_dir);
    wcat(log_path, MAX_PATH, L"\\DualSenseRDR2.log");
    HANDLE f = CreateFileW(log_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    static const char msg[] =
        "[ERROR] DualSenseRDR2\\DualSenseCore.dll could not be loaded - copy the entire DualSenseRDR2 folder from the zip\r\n";
    DWORD written = 0;
    WriteFile(f, msg, (DWORD)(sizeof(msg) - 1), &written, NULL);
    CloseHandle(f);
}

static DWORD WINAPI load_core_thread(LPVOID param) {
    LoaderCtx* ctx = (LoaderCtx*)param;
    HMODULE self = ctx->self;
    HMODULE core = LoadLibraryW(ctx->core_path);
    if (core) bind_core(core);
    else log_missing_core(ctx->asi_dir);
    HeapFree(GetProcessHeap(), 0, ctx);
    FreeLibraryAndExitThread(self, 0);
    return 0;
}

static void script_main(void) {
    int started = 0;
    for (;;) {
        if (!started) {
            dsr2_event_fn ev = (dsr2_event_fn)(uintptr_t)InterlockedCompareExchangePointer(&g_event_fn, NULL, NULL);
            if (ev) {
                ev(1);
                started = 1;
            }
        }
        dsr2_tick_fn tick = (dsr2_tick_fn)(uintptr_t)InterlockedCompareExchangePointer(&g_tick_fn, NULL, NULL);
        if (tick) tick();
        shv_scriptWait(0);
    }
}

BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        shv_scriptRegister(module, script_main);
        HMODULE core = GetModuleHandleW(L"DualSenseCore.dll");
        if (core) {
            bind_core(core);
        } else {
            LoaderCtx* ctx = (LoaderCtx*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(LoaderCtx));
            if (ctx) {
                DWORD n = GetModuleFileNameW(module, ctx->asi_dir, MAX_PATH);
                if (n > 0 && n < MAX_PATH) {
                    DWORD sep = n;
                    while (sep > 0 && ctx->asi_dir[sep - 1] != L'\\' && ctx->asi_dir[sep - 1] != L'/') --sep;
                    if (sep > 0) ctx->asi_dir[sep - 1] = L'\0';
                }
                wcopy(ctx->core_path, MAX_PATH, ctx->asi_dir);
                wcat(ctx->core_path, MAX_PATH, L"\\DualSenseRDR2\\DualSenseCore.dll");
                HANDLE th = CreateThread(NULL, 0, load_core_thread, ctx, CREATE_SUSPENDED, NULL);
                if (th) {
                    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)(uintptr_t)&DllMain, &ctx->self)) {
                        ResumeThread(th);
                        CloseHandle(th);
                    } else {
                        TerminateThread(th, 0);
                        CloseHandle(th);
                        HeapFree(GetProcessHeap(), 0, ctx);
                    }
                } else {
                    HeapFree(GetProcessHeap(), 0, ctx);
                }
            }
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        if (!lpReserved) {
            shv_scriptUnregister(module);
            dsr2_event_fn ev = (dsr2_event_fn)(uintptr_t)InterlockedCompareExchangePointer(&g_event_fn, NULL, NULL);
            if (ev) ev(0);
        }
    }
    return TRUE;
}
