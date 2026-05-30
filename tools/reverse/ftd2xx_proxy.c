/* ftd2xx_proxy.c — MPPS ↔ USB protocol capture proxy DLL (32-bit Windows)
 *
 * Drop-in replacement for ftd2xx.dll placed next to Mpps.exe. It forwards EVERY
 * ftd2xx export to the *renamed* original (ftd2xx_real.dll) and additionally
 * logs the data-carrying / config calls (FT_Write, FT_Read, FT_W32_*File,
 * FT_SetBaudRate, FT_SetLatencyTimer, FT_SetBitMode, FT_Open, …) to
 * mpps_protocol_capture.log.
 *
 * IMPORTANT:
 *  - Mpps.exe is a 32-bit (i386) binary, so this MUST be built 32-bit:
 *        i686-w64-mingw32-gcc  (NOT x86_64).  See tools/reverse/build_proxy.sh.
 *  - The 71 non-hooked exports are forwarded at the PE-loader level via
 *    ftd2xx_proxy.def (EXPORTS name=ftd2xx_real.name) — no code needed for them.
 *    Only the 14 functions below are intercepted here. Forwarding ALL exports is
 *    required: if any export Mpps.exe imports were missing, the process would
 *    fail to load. (The previous version only forwarded 6 → it would crash.)
 *
 * Build: see build_proxy.sh (uses ftd2xx_proxy.def for the forwarders).
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef void*          FT_HANDLE;
typedef unsigned long  FT_STATUS;
typedef unsigned long  ULONG;
typedef unsigned char  UCHAR;

/* ── original (real) function pointer typedefs ─────────────────────────────── */
typedef FT_STATUS (WINAPI *FT_Write_t)(FT_HANDLE, void*, ULONG, ULONG*);
typedef FT_STATUS (WINAPI *FT_Read_t)(FT_HANDLE, void*, ULONG, ULONG*);
typedef FT_STATUS (WINAPI *FT_Open_t)(int, FT_HANDLE*);
typedef FT_STATUS (WINAPI *FT_OpenEx_t)(void*, ULONG, FT_HANDLE*);
typedef FT_STATUS (WINAPI *FT_Close_t)(FT_HANDLE);
typedef FT_STATUS (WINAPI *FT_SetBaudRate_t)(FT_HANDLE, ULONG);
typedef FT_STATUS (WINAPI *FT_SetLatencyTimer_t)(FT_HANDLE, UCHAR);
typedef FT_STATUS (WINAPI *FT_SetTimeouts_t)(FT_HANDLE, ULONG, ULONG);
typedef FT_STATUS (WINAPI *FT_Purge_t)(FT_HANDLE, ULONG);
typedef FT_STATUS (WINAPI *FT_GetStatus_t)(FT_HANDLE, ULONG*, ULONG*, ULONG*);
typedef FT_STATUS (WINAPI *FT_GetQueueStatus_t)(FT_HANDLE, ULONG*);
typedef FT_STATUS (WINAPI *FT_SetBitMode_t)(FT_HANDLE, UCHAR, UCHAR);
typedef BOOL      (WINAPI *FT_W32_WriteFile_t)(FT_HANDLE, void*, ULONG, ULONG*, void*);
typedef BOOL      (WINAPI *FT_W32_ReadFile_t)(FT_HANDLE, void*, ULONG, ULONG*, void*);

static HMODULE g_real = NULL;
static FILE*   g_log  = NULL;

static FT_Write_t           r_FT_Write;
static FT_Read_t            r_FT_Read;
static FT_Open_t            r_FT_Open;
static FT_OpenEx_t          r_FT_OpenEx;
static FT_Close_t           r_FT_Close;
static FT_SetBaudRate_t     r_FT_SetBaudRate;
static FT_SetLatencyTimer_t r_FT_SetLatencyTimer;
static FT_SetTimeouts_t     r_FT_SetTimeouts;
static FT_Purge_t           r_FT_Purge;
static FT_GetStatus_t       r_FT_GetStatus;
static FT_GetQueueStatus_t  r_FT_GetQueueStatus;
static FT_SetBitMode_t      r_FT_SetBitMode;
static FT_W32_WriteFile_t   r_FT_W32_WriteFile;
static FT_W32_ReadFile_t    r_FT_W32_ReadFile;

static void log_hex(const char* dir, const void* buf, ULONG len) {
    if (!g_log) return;
    SYSTEMTIME st; GetLocalTime(&st);
    fprintf(g_log, "[%02d:%02d:%02d.%03d] %s (%lu bytes): ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, dir, len);
    const unsigned char* p = (const unsigned char*)buf;
    for (ULONG i = 0; i < len; i++) {
        fprintf(g_log, "%02X ", p[i]);
        if ((i + 1) % 16 == 0) fprintf(g_log, "\n    ");
    }
    fprintf(g_log, "\n");
    fflush(g_log);
}

#define BIND(name) r_##name = (name##_t)GetProcAddress(g_real, #name)

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)h; (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        g_log = fopen("mpps_protocol_capture.log", "a");
        if (g_log) fprintf(g_log, "\n=== Capture session start ===\n");
        g_real = LoadLibraryA("ftd2xx_real.dll");
        if (!g_real) {
            if (g_log) fprintf(g_log, "[ERROR] ftd2xx_real.dll not found\n");
            return FALSE;
        }
        BIND(FT_Write); BIND(FT_Read); BIND(FT_Open); BIND(FT_OpenEx);
        BIND(FT_Close); BIND(FT_SetBaudRate); BIND(FT_SetLatencyTimer);
        BIND(FT_SetTimeouts); BIND(FT_Purge); BIND(FT_GetStatus);
        BIND(FT_GetQueueStatus); BIND(FT_SetBitMode);
        BIND(FT_W32_WriteFile); BIND(FT_W32_ReadFile);
        if (g_log) fprintf(g_log, "[OK] proxy initialised (hooks bound)\n");
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_log) { fprintf(g_log, "=== Capture session end ===\n"); fclose(g_log); }
        if (g_real) FreeLibrary(g_real);
    }
    return TRUE;
}

/* ── hooked exports (names exported via ftd2xx_proxy.def) ───────────────────── */
FT_STATUS WINAPI FT_Write(FT_HANDLE h, void* b, ULONG n, ULONG* w) {
    log_hex("TX -->", b, n);
    return r_FT_Write(h, b, n, w);
}
FT_STATUS WINAPI FT_Read(FT_HANDLE h, void* b, ULONG n, ULONG* rd) {
    FT_STATUS s = r_FT_Read(h, b, n, rd);
    if (s == 0 && rd && *rd) log_hex("<-- RX", b, *rd);
    return s;
}
BOOL WINAPI FT_W32_WriteFile(FT_HANDLE h, void* b, ULONG n, ULONG* w, void* o) {
    log_hex("TX --> (W32)", b, n);
    return r_FT_W32_WriteFile(h, b, n, w, o);
}
BOOL WINAPI FT_W32_ReadFile(FT_HANDLE h, void* b, ULONG n, ULONG* rd, void* o) {
    BOOL s = r_FT_W32_ReadFile(h, b, n, rd, o);
    if (s && rd && *rd) log_hex("<-- RX (W32)", b, *rd);
    return s;
}
FT_STATUS WINAPI FT_Open(int dev, FT_HANDLE* ph) {
    if (g_log) fprintf(g_log, "[FT_Open] device=%d\n", dev);
    return r_FT_Open(dev, ph);
}
FT_STATUS WINAPI FT_OpenEx(void* arg, ULONG flags, FT_HANDLE* ph) {
    if (g_log) fprintf(g_log, "[FT_OpenEx] flags=0x%lx\n", flags);
    return r_FT_OpenEx(arg, flags, ph);
}
FT_STATUS WINAPI FT_Close(FT_HANDLE h) {
    if (g_log) fprintf(g_log, "[FT_Close]\n");
    return r_FT_Close(h);
}
FT_STATUS WINAPI FT_SetBaudRate(FT_HANDLE h, ULONG baud) {
    if (g_log) fprintf(g_log, "[FT_SetBaudRate] %lu\n", baud);
    return r_FT_SetBaudRate(h, baud);
}
FT_STATUS WINAPI FT_SetLatencyTimer(FT_HANDLE h, UCHAR ms) {
    if (g_log) fprintf(g_log, "[FT_SetLatencyTimer] %u ms\n", ms);
    return r_FT_SetLatencyTimer(h, ms);
}
FT_STATUS WINAPI FT_SetTimeouts(FT_HANDLE h, ULONG rd, ULONG wr) {
    if (g_log) fprintf(g_log, "[FT_SetTimeouts] read=%lu write=%lu ms\n", rd, wr);
    return r_FT_SetTimeouts(h, rd, wr);
}
FT_STATUS WINAPI FT_Purge(FT_HANDLE h, ULONG mask) {
    if (g_log) fprintf(g_log, "[FT_Purge] mask=0x%lx\n", mask);
    return r_FT_Purge(h, mask);
}
FT_STATUS WINAPI FT_SetBitMode(FT_HANDLE h, UCHAR mask, UCHAR mode) {
    if (g_log) fprintf(g_log, "[FT_SetBitMode] mask=0x%02X mode=0x%02X\n", mask, mode);
    return r_FT_SetBitMode(h, mask, mode);
}
FT_STATUS WINAPI FT_GetStatus(FT_HANDLE h, ULONG* rx, ULONG* tx, ULONG* ev) {
    return r_FT_GetStatus(h, rx, tx, ev);
}
FT_STATUS WINAPI FT_GetQueueStatus(FT_HANDLE h, ULONG* rx) {
    return r_FT_GetQueueStatus(h, rx);
}
