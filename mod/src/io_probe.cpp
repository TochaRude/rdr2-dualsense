// I/O probe (debug log only, read only): which parts of the game's audio archives (x64\audio\sfx\*.rpf) the game
// reads, when - to find out whether each spoken line / cutscene is streamed from disk and can be identified
// exactly (archive + offset -> file -> stream). Hooks ntdll!NtReadFile with a small trampoline; the original call
// always runs unchanged, the hook only records (handle, offset, size, time) for audio archives.
#include "io_probe.h"

#include "common.h"
#include "log.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace dsr2 {

namespace {

using NtReadFileFn = LONG(NTAPI*)(HANDLE, HANDLE, PVOID, PVOID, PVOID, PVOID, ULONG, PLARGE_INTEGER, PULONG);
NtReadFileFn g_orig = nullptr;

struct Rec {
    std::atomic<uint64_t> seq{0};
    uint64_t ft = 0;
    HANDLE h = nullptr;
    int64_t off = 0;
    uint32_t len = 0;
};
constexpr size_t kRing = 1 << 14;
Rec g_ring[kRing];
std::atomic<uint64_t> g_head{0}, g_tail{0};
std::atomic<bool> g_stop{false};
uint8_t* g_hooked_fn = nullptr;
uint64_t g_orig_bytes = 0;

struct ProbeState {
    std::thread drain;
    std::mutex mu;                                   // guards the handle map
    std::unordered_map<HANDLE, int> kind;            // 1 = audio archive, 0 = other
    std::unordered_map<HANDLE, std::string> name;
};
ProbeState& probe_state() {
    static auto* s = new ProbeState();
    return *s;
}
thread_local bool t_busy = false;

int kind_of(HANDLE h) {
    ProbeState& ps = probe_state();
    {
        std::lock_guard<std::mutex> l(ps.mu);
        auto it = ps.kind.find(h);
        if (it != ps.kind.end()) return it->second;
    }
    int k = 0;
    std::string name;
    // Never call GetFinalPathNameByHandleW on pipes/sockets/devices (can deadlock inside NtReadFile)
    if (GetFileType(h) == FILE_TYPE_DISK) {
        wchar_t buf[1024];
        DWORD n = GetFinalPathNameByHandleW(h, buf, 1023, FILE_NAME_NORMALIZED);
        if (n > 0 && n < 1023) {
            std::wstring p(buf, n);
            size_t i = p.find(L"\\x64\\audio\\");
            if (i != std::wstring::npos) {
                k = 1;
                for (wchar_t c : p.substr(i + 1)) name += char(c < 128 ? c : '?');
            }
        }
    }
    std::lock_guard<std::mutex> l(ps.mu);
    if (ps.kind.size() > 4096) { ps.kind.clear(); ps.name.clear(); }
    ps.kind[h] = k;
    if (k) ps.name[h] = name;
    return k;
}

LONG NTAPI hk_read(HANDLE h, HANDLE ev, PVOID apc, PVOID ctx, PVOID iosb, PVOID buf, ULONG len, PLARGE_INTEGER off,
                   PULONG key) {
    if (!t_busy && off && !g_stop.load(std::memory_order_relaxed)) {
        t_busy = true;
        if (kind_of(h) == 1) {
            if (g_head.load(std::memory_order_relaxed) - g_tail.load(std::memory_order_acquire) < kRing) {
                FILETIME ft;
                GetSystemTimePreciseAsFileTime(&ft);
                uint64_t i = g_head.fetch_add(1, std::memory_order_relaxed);
                Rec& slot = g_ring[i % kRing];
                slot.ft = (uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
                slot.h = h;
                slot.off = off->QuadPart;
                slot.len = len;
                slot.seq.store(i + 1, std::memory_order_release);
            }
        }
        t_busy = false;
    }
    return g_orig(h, ev, apc, ctx, iosb, buf, len, off, key);
}

void* alloc_near(void* target) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    uintptr_t t = uintptr_t(target), gran = si.dwAllocationGranularity;
    for (uintptr_t d = gran; d < 0x70000000; d += gran) {
        uintptr_t cands[2] = {t > d ? t - d : 0, t + d};
        for (uintptr_t cand : cands) {
            if (!cand) continue;
            void* p = VirtualAlloc(reinterpret_cast<void*>(cand & ~(gran - 1)), 4096, MEM_COMMIT | MEM_RESERVE,
                                   PAGE_READWRITE);
            if (p) return p;
        }
    }
    return nullptr;
}

void drain() {
    t_busy = true;                                 // our own log writes must not be recorded
    while (!g_stop.load(std::memory_order_relaxed)) {
        Sleep(25);
        uint64_t head = g_head.load(std::memory_order_acquire), tail = g_tail.load(std::memory_order_relaxed);
        if (head - tail > kRing) {
            LOGD("io: %llu reads lost (ring full)", (unsigned long long)(head - tail - kRing));
            tail = head - kRing;
        }
        for (; tail < head; ++tail) {
            Rec& slot = g_ring[tail % kRing];
            if (slot.seq.load(std::memory_order_acquire) != tail + 1) break;   // producer still writing
            uint64_t rft = slot.ft;
            HANDLE rh = slot.h;
            int64_t roff = slot.off;
            uint32_t rlen = slot.len;
            std::string name;
            {
                ProbeState& ps = probe_state();
                std::lock_guard<std::mutex> l(ps.mu);
                auto it = ps.name.find(rh);
                if (it != ps.name.end()) name = it->second;
            }
            FILETIME ft{DWORD(rft), DWORD(rft >> 32)}, lt;
            SYSTEMTIME st;
            FileTimeToLocalFileTime(&ft, &lt);
            FileTimeToSystemTime(&lt, &st);
            LOGD("io: %02d:%02d:%02d.%03d %s off 0x%llX len %u", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                 name.c_str(), (unsigned long long)roff, rlen);
        }
        g_tail.store(tail, std::memory_order_release);
    }
}

}  // namespace

void io_probe_start() {
    if (g_hooked_fn) return;
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto* f = reinterpret_cast<uint8_t*>(nt ? GetProcAddress(nt, "NtReadFile") : nullptr);
    // x64 syscall stub: 4C 8B D1 (mov r10, rcx)  B8 xx xx xx xx (mov eax, number) - 8 self-contained bytes
    if (!f || !(f[0] == 0x4C && f[1] == 0x8B && f[2] == 0xD1 && f[3] == 0xB8)) {
        LOGW("io probe: NtReadFile has an unexpected start - not hooked");
        return;
    }
    auto* tramp = static_cast<uint8_t*>(alloc_near(f));
    if (!tramp) { LOGW("io probe: no memory near ntdll"); return; }
    std::memcpy(&g_orig_bytes, f, 8);
    std::memcpy(tramp, f, 8);                                     // the two original instructions
    tramp[8] = 0xFF; tramp[9] = 0x25; std::memset(tramp + 10, 0, 4);  // jmp [rip+0] -> back to NtReadFile+8
    uint64_t back = uint64_t(f + 8);
    std::memcpy(tramp + 14, &back, 8);
    uint8_t* relay = tramp + 32;                                  // jmp [rip+0] -> our hook
    relay[0] = 0xFF; relay[1] = 0x25; std::memset(relay + 2, 0, 4);
    uint64_t hk = uint64_t(&hk_read);
    std::memcpy(relay + 6, &hk, 8);
    intptr_t diff = intptr_t(relay) - (intptr_t(f) + 5);
    if (diff < INT32_MIN || diff > INT32_MAX) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        return;
    }
    DWORD tramp_old = 0;
    if (!VirtualProtect(tramp, 4096, PAGE_EXECUTE_READ, &tramp_old)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        return;
    }
    FlushInstructionCache(GetCurrentProcess(), tramp, 64);
    g_orig = reinterpret_cast<NtReadFileFn>(tramp);
    // replace the first 8 bytes in one atomic write: jmp rel32 to the relay + 3 nops
    uint8_t patch[8] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
    int32_t rel = int32_t(diff);
    std::memcpy(patch + 1, &rel, 4);
    DWORD old = 0;
    if (!VirtualProtect(f, 8, PAGE_EXECUTE_READWRITE, &old)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        g_orig = nullptr;
        LOGW("io probe: cannot patch ntdll");
        return;
    }
    uint64_t v = 0;
    std::memcpy(&v, patch, 8);
    InterlockedExchange64(reinterpret_cast<volatile LONG64*>(f), LONG64(v));
    VirtualProtect(f, 8, old, &old);
    FlushInstructionCache(GetCurrentProcess(), f, 8);
    g_hooked_fn = f;
    g_stop = false;
    probe_state().drain = std::thread(drain);
    LOGI("io probe: logging reads of the game's audio archives (debug)");
}

void io_probe_stop() {
    g_stop = true;
    if (g_hooked_fn) {
        DWORD old = 0;
        if (VirtualProtect(g_hooked_fn, 8, PAGE_EXECUTE_READWRITE, &old)) {
            InterlockedExchange64(reinterpret_cast<volatile LONG64*>(g_hooked_fn), LONG64(g_orig_bytes));
            VirtualProtect(g_hooked_fn, 8, old, &old);
            FlushInstructionCache(GetCurrentProcess(), g_hooked_fn, 8);
        }
        g_hooked_fn = nullptr;
    }
    std::thread& t = probe_state().drain;
    if (t.joinable()) t.join();
}

}  // namespace dsr2
