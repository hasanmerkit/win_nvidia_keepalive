/*
 * gpu_keepalive.c
 * ---------------------------------------------------------------------------
 * Keeps the NVIDIA GPU awake with ZERO compute load.
 *
 * How it works:
 *   - Loads nvcuda.dll (CUDA Driver API) at runtime - no CUDA Toolkit / nvcc needed.
 *   - Opens a CUDA context so the GPU and driver stay initialized.
 *   - Then just sleeps. No kernels run => GPU-Util 0%.
 *   - If the GPU disappears (e.g. MUX switch disables the dGPU), detects it
 *     and shuts down cleanly.
 *
 * Built as a GUI subsystem app => runs hidden, no console/CMD window.
 *
 * Parameters:
 *   (none)       Run hidden in the background and keep the GPU awake.
 *   --help       Show the help message.
 *   --startup    Copy itself to the Windows Startup folder.
 *                Administrator -> all users; otherwise current user only.
 *
 * Build:
 *   tcc\tcc.exe gpu_keepalive.c -o gpu_keepalive.exe -luser32 -Wl,-subsystem=windows
 * ---------------------------------------------------------------------------
 */

#include <windows.h>
#include <string.h>

/* ---- CUDA Driver API type definitions (no cuda.h needed) ---- */
typedef int   CUresult;
typedef int   CUdevice;
typedef void* CUcontext;
#define CUDA_SUCCESS 0

typedef CUresult (*pfn_cuInit)(unsigned int);
typedef CUresult (*pfn_cuDeviceGetCount)(int*);
typedef CUresult (*pfn_cuDeviceGet)(CUdevice*, int);
typedef CUresult (*pfn_cuCtxCreate)(CUcontext*, unsigned int, CUdevice);
typedef CUresult (*pfn_cuCtxSynchronize)(void);
typedef CUresult (*pfn_cuCtxDestroy)(CUcontext);

/* ---------------------------------------------------------------------------
 * Are we running elevated (Administrator)?
 * Loaded dynamically from advapi32.dll - no extra headers or linker flags.
 * ------------------------------------------------------------------------- */
static int is_elevated(void) {
    HMODULE adv = LoadLibraryA("advapi32.dll");
    if (!adv) return 0;

    typedef BOOL (WINAPI *pfn_OpenProcessToken)(HANDLE, DWORD, PHANDLE);
    typedef BOOL (WINAPI *pfn_GetTokenInformation)(HANDLE, int, LPVOID, DWORD, PDWORD);

    pfn_OpenProcessToken    OpenProcessToken_    =
        (pfn_OpenProcessToken)   GetProcAddress(adv, "OpenProcessToken");
    pfn_GetTokenInformation GetTokenInformation_ =
        (pfn_GetTokenInformation)GetProcAddress(adv, "GetTokenInformation");

    int elevated = 0;
    if (OpenProcessToken_ && GetTokenInformation_) {
        HANDLE tok = NULL;
        if (OpenProcessToken_(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
            DWORD info = 0, retlen = 0;
            /* TokenElevation == 20; non-zero info means elevated */
            if (GetTokenInformation_(tok, 20, &info, sizeof(info), &retlen)) {
                elevated = (info != 0);
            }
            CloseHandle(tok);
        }
    }
    FreeLibrary(adv);
    return elevated;
}

/* ---------------------------------------------------------------------------
 * --help: show usage info via MessageBox (no console in GUI subsystem).
 * ------------------------------------------------------------------------- */
static void show_help(void) {
    MessageBoxA(NULL,
        "GPU Keep-Alive Utility\r\n"
        "Keeps the NVIDIA GPU awake with ZERO compute load.\r\n\r\n"
        "Usage:\r\n"
        "  gpu_keepalive.exe\r\n"
        "      Run hidden in the background and keep the GPU awake.\r\n\r\n"
        "  gpu_keepalive.exe --help\r\n"
        "      Show this help message.\r\n\r\n"
        "  gpu_keepalive.exe --startup\r\n"
        "      Install this program to run automatically at Windows startup.\r\n"
        "      If launched as Administrator, it installs for ALL users;\r\n"
        "      otherwise it installs only for the current user.",
        "GPU Keep-Alive - Help",
        MB_OK | MB_ICONINFORMATION);
}

/* ---------------------------------------------------------------------------
 * --startup: copy ourselves to the Startup folder.
 *   Administrator -> %ProgramData%\Microsoft\Windows\Start Menu\Programs\Startup
 *   Normal user   -> %APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup
 * ------------------------------------------------------------------------- */
static void install_startup(void) {
    char self[MAX_PATH];
    if (!GetModuleFileNameA(NULL, self, MAX_PATH)) {
        MessageBoxA(NULL, "Could not determine own path.",
                    "GPU Keep-Alive", MB_OK | MB_ICONERROR);
        return;
    }

    int admin = is_elevated();

    char base[MAX_PATH];
    const char *var = admin ? "ProgramData" : "APPDATA";
    if (!GetEnvironmentVariableA(var, base, MAX_PATH)) {
        MessageBoxA(NULL, "Could not resolve the startup base folder.",
                    "GPU Keep-Alive", MB_OK | MB_ICONERROR);
        return;
    }

    char dest[MAX_PATH];
    lstrcpyA(dest, base);
    lstrcatA(dest, "\\Microsoft\\Windows\\Start Menu\\Programs\\Startup\\gpu_keepalive.exe");

    if (CopyFileA(self, dest, FALSE)) {
        char msg[MAX_PATH + 128];
        lstrcpyA(msg, admin
            ? "Installed at startup for ALL users:\r\n\r\n"
            : "Installed at startup for the current user:\r\n\r\n");
        lstrcatA(msg, dest);
        MessageBoxA(NULL, msg, "GPU Keep-Alive - Startup",
                    MB_OK | MB_ICONINFORMATION);
    } else {
        char msg[256];
        DWORD e = GetLastError();
        wsprintfA(msg, "Failed to copy to the startup folder. (Win32 error %lu)\r\n"
                       "Tip: run as Administrator to install for all users.", e);
        MessageBoxA(NULL, msg, "GPU Keep-Alive - Startup",
                    MB_OK | MB_ICONERROR);
    }
}

/* ---------------------------------------------------------------------------
 * Main work: open a CUDA context and sleep forever. Zero load.
 * ------------------------------------------------------------------------- */
static int run_keepalive(void) {
    HMODULE dll = LoadLibraryA("nvcuda.dll");
    if (!dll) return 1;  /* no driver; exit silently */

    pfn_cuInit           cuInit           = (pfn_cuInit)          GetProcAddress(dll, "cuInit");
    pfn_cuDeviceGetCount cuDeviceGetCount = (pfn_cuDeviceGetCount)GetProcAddress(dll, "cuDeviceGetCount");
    pfn_cuDeviceGet      cuDeviceGet      = (pfn_cuDeviceGet)     GetProcAddress(dll, "cuDeviceGet");
    pfn_cuCtxCreate      cuCtxCreate      = (pfn_cuCtxCreate)     GetProcAddress(dll, "cuCtxCreate_v2");
    pfn_cuCtxSynchronize cuCtxSynchronize = (pfn_cuCtxSynchronize)GetProcAddress(dll, "cuCtxSynchronize");
    pfn_cuCtxDestroy     cuCtxDestroy     = (pfn_cuCtxDestroy)    GetProcAddress(dll, "cuCtxDestroy_v2");

    if (!cuInit || !cuDeviceGet || !cuCtxCreate || !cuCtxDestroy) return 1;
    if (cuInit(0) != CUDA_SUCCESS) return 1;

    /* No NVIDIA GPU in the system -> exit silently without opening anything. */
    if (cuDeviceGetCount) {
        int count = 0;
        if (cuDeviceGetCount(&count) != CUDA_SUCCESS || count <= 0) return 1;
    }

    CUdevice dev = 0;
    if (cuDeviceGet(&dev, 0) != CUDA_SUCCESS) return 1;

    CUcontext ctx = NULL;
    if (cuCtxCreate(&ctx, 0, dev) != CUDA_SUCCESS) return 1;

    /* Sleep forever. No kernels, no compute => GPU-Util 0%.
     * Every 5 s do a cheap health check to detect GPU removal (e.g. MUX switch):
     *   - cuDeviceGetCount returns 0 / error when the dGPU disappears.
     *   - cuCtxSynchronize errors when the context becomes invalid.
     * Require 2 consecutive failures to avoid false positives. */
    unsigned long ticks = 0;
    int miss = 0;
    for (;;) {
        Sleep(1000);
        ticks++;

        if (ticks % 5 == 0) {
            int gone = 0;

            /* 1) Is the GPU still listed? */
            if (cuDeviceGetCount) {
                int count = 0;
                if (cuDeviceGetCount(&count) != CUDA_SUCCESS || count <= 0)
                    gone = 1;
            }
            /* 2) Is the context still valid? Also keeps it alive; no measurable load. */
            if (!gone && cuCtxSynchronize) {
                if (cuCtxSynchronize() != CUDA_SUCCESS)
                    gone = 1;
            }

            if (gone) {
                if (++miss >= 2)
                    break;   /* GPU is really gone -> clean exit */
            } else {
                miss = 0;
            }
        }
    }

    /* GPU removed: release resources and exit. */
    cuCtxDestroy(ctx);
    FreeLibrary(dll);
    return 0;
}

int main(void) {
    /* Read the command line directly - argv is unreliable in GUI subsystem. */
    const char *cmd = GetCommandLineA();

    if (strstr(cmd, "--help")) {
        show_help();
        return 0;
    }
    if (strstr(cmd, "--startup")) {
        install_startup();
        return 0;
    }

    return run_keepalive();
}
