#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <mutex>

namespace dsr2::log {

static std::mutex& log_mu() {
    static auto* m = new std::mutex();
    return *m;
}
static FILE* g_file = nullptr;
static std::atomic<bool> g_debug{false};
static int g_unflushed = 0;
static ULONGLONG g_last_flush = 0;

void open(const std::wstring& path) {
    std::lock_guard<std::mutex> l(log_mu());
    if (g_file) return;
    g_file = _wfopen(path.c_str(), L"w");
}

void close() {
    std::lock_guard<std::mutex> l(log_mu());
    if (g_file) {
        fflush(g_file);
        fclose(g_file);
    }
    g_file = nullptr;
    g_unflushed = 0;
    g_last_flush = 0;
}

void set_debug(bool on) { g_debug.store(on, std::memory_order_relaxed); }
bool debug_enabled() { return g_debug.load(std::memory_order_relaxed); }

void write(const char* level, const char* fmt, ...) {
    char msg[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::lock_guard<std::mutex> l(log_mu());
    ULONGLONG now_ms = GetTickCount64();
    FILE* out = g_file ? g_file : stderr;
    fprintf(out, "%02d:%02d:%02d.%03d [%s] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, level, msg);
    bool urgent = !g_file || !level || level[0] != 'D';
    if (urgent || ++g_unflushed >= 16 || now_ms - g_last_flush >= 250) {
        fflush(out);
        g_unflushed = 0;
        g_last_flush = now_ms;
    }
}

}  // namespace dsr2::log
