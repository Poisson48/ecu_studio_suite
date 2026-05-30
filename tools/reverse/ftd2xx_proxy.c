/* ftd2xx_proxy.c
 * DLL proxy pour intercepter toutes les communications MPPS ↔ USB
 * Compilé en DLL Windows, placé dans le dossier de l'exe MPPS
 * Forwarde vers ftd2xx_real.dll (renommer l'original avant de copier celle-ci)
 *
 * Compiler : x86_64-w64-mingw32-gcc -shared -o ftd2xx.dll ftd2xx_proxy.c -Wl,--kill-at -static-libgcc
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

typedef void*          FT_HANDLE;
typedef unsigned long  FT_STATUS;
typedef unsigned long  DWORD;

typedef FT_STATUS (__stdcall *FT_Write_t)(FT_HANDLE, void*, DWORD, DWORD*);
typedef FT_STATUS (__stdcall *FT_Read_t)(FT_HANDLE, void*, DWORD, DWORD*);
typedef FT_STATUS (__stdcall *FT_Open_t)(int, FT_HANDLE*);
typedef FT_STATUS (__stdcall *FT_Close_t)(FT_HANDLE);
typedef FT_STATUS (__stdcall *FT_SetBaudRate_t)(FT_HANDLE, DWORD);
typedef FT_STATUS (__stdcall *FT_SetTimeouts_t)(FT_HANDLE, DWORD, DWORD);

static HMODULE          real_dll        = NULL;
static FILE*            log_file        = NULL;
static FT_Write_t       real_FT_Write   = NULL;
static FT_Read_t        real_FT_Read    = NULL;
static FT_Open_t        real_FT_Open    = NULL;
static FT_Close_t       real_FT_Close   = NULL;
static FT_SetBaudRate_t real_FT_SetBaud = NULL;
static FT_SetTimeouts_t real_FT_SetTO   = NULL;

static void log_hex(const char* dir, const void* buf, DWORD len) {
    if (!log_file) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(log_file, "[%02d:%02d:%02d.%03d] %s (%lu bytes): ",
            st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, dir, len);
    const unsigned char* p = (const unsigned char*)buf;
    for (DWORD i = 0; i < len; i++) {
        fprintf(log_file, "%02X ", p[i]);
        if ((i + 1) % 16 == 0) fprintf(log_file, "\n    ");
    }
    fprintf(log_file, "\n");
    if (len >= 3 && p[0] == 0x68)
        fprintf(log_file, "    → STX=0x68 LEN=%02X CMD=%02X\n", p[1], p[2]);
    else if (len >= 1 && (p[0] & 0x80))
        fprintf(log_file, "    → RESPONSE ACK=0x%02X\n", p[0]);
    fflush(log_file);
}

BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID lpReserved) {
    (void)hInstance; (void)lpReserved;
    if (fdwReason == DLL_PROCESS_ATTACH) {
        log_file = fopen("mpps_protocol_capture.log", "a");
        if (log_file) fprintf(log_file, "\n=== Session démarrée ===\n");
        real_dll = LoadLibraryA("ftd2xx_real.dll");
        if (!real_dll) {
            if (log_file) fprintf(log_file, "[ERREUR] ftd2xx_real.dll introuvable\n");
            return FALSE;
        }
        real_FT_Write = (FT_Write_t)      GetProcAddress(real_dll, "FT_Write");
        real_FT_Read  = (FT_Read_t)       GetProcAddress(real_dll, "FT_Read");
        real_FT_Open  = (FT_Open_t)       GetProcAddress(real_dll, "FT_Open");
        real_FT_Close = (FT_Close_t)      GetProcAddress(real_dll, "FT_Close");
        real_FT_SetBaud = (FT_SetBaudRate_t)GetProcAddress(real_dll, "FT_SetBaudRate");
        real_FT_SetTO   = (FT_SetTimeouts_t)GetProcAddress(real_dll, "FT_SetTimeouts");
        if (log_file) fprintf(log_file, "[OK] Proxy DLL initialisé\n");
    } else if (fdwReason == DLL_PROCESS_DETACH) {
        if (log_file) { fprintf(log_file, "=== Session terminée ===\n"); fclose(log_file); }
        if (real_dll) FreeLibrary(real_dll);
    }
    return TRUE;
}

__declspec(dllexport) FT_STATUS __stdcall FT_Write(
    FT_HANDLE h, void* buf, DWORD n, DWORD* written)
{
    log_hex("TX →", buf, n);
    return real_FT_Write(h, buf, n, written);
}

__declspec(dllexport) FT_STATUS __stdcall FT_Read(
    FT_HANDLE h, void* buf, DWORD n, DWORD* read)
{
    FT_STATUS ret = real_FT_Read(h, buf, n, read);
    if (ret == 0 && *read > 0) log_hex("← RX", buf, *read);
    return ret;
}

__declspec(dllexport) FT_STATUS __stdcall FT_Open(int dev, FT_HANDLE* ph) {
    if (log_file) fprintf(log_file, "[FT_Open] device=%d\n", dev);
    return real_FT_Open(dev, ph);
}

__declspec(dllexport) FT_STATUS __stdcall FT_Close(FT_HANDLE h) {
    if (log_file) fprintf(log_file, "[FT_Close]\n");
    return real_FT_Close(h);
}

__declspec(dllexport) FT_STATUS __stdcall FT_SetBaudRate(FT_HANDLE h, DWORD baud) {
    if (log_file) fprintf(log_file, "[FT_SetBaudRate] %lu\n", baud);
    return real_FT_SetBaud(h, baud);
}

__declspec(dllexport) FT_STATUS __stdcall FT_SetTimeouts(FT_HANDLE h, DWORD rd, DWORD wr) {
    if (log_file) fprintf(log_file, "[FT_SetTimeouts] read=%lu write=%lu ms\n", rd, wr);
    return real_FT_SetTO(h, rd, wr);
}
