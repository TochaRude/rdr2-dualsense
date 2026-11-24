// Minimal ScriptHookRDR2.dll mock for testing ScriptHookRDR2 V2 FreeLibrary/LoadLibrary reloads on Windows.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>

typedef void (*ScriptMainFn)(void);

static ScriptMainFn g_script_main = NULL;
static int g_reg_count = 0;
static int g_unreg_count = 0;
static LPVOID g_caller_fiber = NULL;
static int g_fiber_active = 0;
static uint64_t g_zero_ret[4] = {0, 0, 0, 0};

void mock_scriptRegister(HMODULE module, ScriptMainFn fn) {
    (void)module;
    g_script_main = fn;
    ++g_reg_count;
}

void mock_scriptRegisterAdditionalThread(HMODULE module, ScriptMainFn fn) {
    (void)module;
    (void)fn;
}

void mock_scriptUnregister(HMODULE module) {
    (void)module;
    g_script_main = NULL;
    ++g_unreg_count;
}

void mock_scriptWait(DWORD ms) {
    (void)ms;
    if (g_fiber_active && g_caller_fiber) SwitchToFiber(g_caller_fiber);
}

void mock_nativeInit(uint64_t hash) { (void)hash; }
void mock_nativePush64(uint64_t val) { (void)val; }
uint64_t* mock_nativeCall(void) { return g_zero_ret; }
int mock_worldGetAllPeds(int* arr, int size) { (void)arr; (void)size; return 0; }
int mock_getGameVersion(void) { return 0; }
uint64_t* mock_getGlobalPtr(int id) { (void)id; return g_zero_ret; }
uint8_t* mock_getScriptHandleBaseAddress(int handle) { (void)handle; return NULL; }

static VOID CALLBACK script_fiber_proc(LPVOID param) {
    (void)param;
    if (g_script_main) g_script_main();
    if (g_caller_fiber) SwitchToFiber(g_caller_fiber);
}

__declspec(dllexport) void mock_shv_counts(int* reg, int* unreg) {
    if (reg) *reg = g_reg_count;
    if (unreg) *unreg = g_unreg_count;
}

__declspec(dllexport) int mock_shv_run_once(void) {
    if (!g_script_main) return 0;
    if (!IsThreadAFiber()) {
        g_caller_fiber = ConvertThreadToFiber(NULL);
    } else {
        g_caller_fiber = GetCurrentFiber();
    }
    if (!g_caller_fiber) return 0;
    LPVOID sf = CreateFiber(0, script_fiber_proc, NULL);
    if (!sf) return 0;
    g_fiber_active = 1;
    SwitchToFiber(sf);
    g_fiber_active = 0;
    DeleteFiber(sf);
    return 1;
}
