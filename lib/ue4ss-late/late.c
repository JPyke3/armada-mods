// Stands in for UE4SS.dll so the stock proxy loads this first. Some games' start-up
// protection fails while UE4SS scans the process, so the real one is loaded once the game
// window exists. Also reads memory for the provider script, which UE4SS Lua cannot:
// "address length" lines (hex) in peek.req are answered with the bytes in peek.bin.
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

static BOOL CALLBACK find(HWND w, LPARAM out) {
    DWORD pid; char cls[64];
    GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(w)) return TRUE;
    if (!GetClassNameA(w, cls, sizeof cls) || lstrcmpA(cls, "UnrealWindow")) return TRUE;
    *(BOOL *)out = TRUE;
    return FALSE;
}

static BOOL readable(const char *p, size_t n) {
    while (n) {
        MEMORY_BASIC_INFORMATION m;
        if (!VirtualQuery(p, &m, sizeof m) || m.State != MEM_COMMIT || (m.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
            !(m.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) return FALSE;
        size_t left = (const char *)m.BaseAddress + m.RegionSize - p;
        if (left >= n) return TRUE;
        p += left; n -= left;
    }
    return TRUE;
}

static void serve(const char *dir) {
    char req[600], tmp[600], bin[600], line[64];
    snprintf(req, sizeof req, "%s\\peek.req", dir);
    snprintf(tmp, sizeof tmp, "%s\\peek.tmp", dir);
    snprintf(bin, sizeof bin, "%s\\peek.bin", dir);
    static char zero[4096];
    for (;;) {
        Sleep(40);
        FILE *in = fopen(req, "r");
        if (!in) continue;
        FILE *out = fopen(tmp, "wb");
        while (out && fgets(line, sizeof line, in)) {
            char *end;
            unsigned long long addr = strtoull(line, &end, 16), len = strtoull(end, NULL, 16);
            if (!len || len > 0x100000) continue;
            if (readable((const char *)(size_t)addr, len)) fwrite((const void *)(size_t)addr, 1, len, out);
            else for (size_t left = len; left; ) { size_t n = left < sizeof zero ? left : sizeof zero; fwrite(zero, 1, n, out); left -= n; }
        }
        fclose(in);
        DeleteFileA(req);
        if (out) { fclose(out); MoveFileExA(tmp, bin, MOVEFILE_REPLACE_EXISTING); }
    }
}

static DWORD WINAPI run(void *self) {
    BOOL up = FALSE;
    while (!up) { Sleep(500); EnumWindows(find, (LPARAM)&up); }
    Sleep(3000);
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    while (n && path[n - 1] != L'\\') n--;
    lstrcpyW(path + n, L"ue4ss\\UE4SS.dll");
    LoadLibraryW(path);
    char link[512], dir[520] = "Z:";
    if (!GetEnvironmentVariableA("ARMADA_MOD_LINK", link, sizeof link)) return 0;
    for (char *c = link; *c; c++) if (*c == '/') *c = '\\';
    lstrcatA(dir, link);
    serve(dir);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, void *reserved) {
    if (reason == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(self); CloseHandle(CreateThread(NULL, 0, run, self, 0, NULL)); }
    return TRUE;
}
