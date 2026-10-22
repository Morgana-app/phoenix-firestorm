#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include "resize_policy.hpp"
static struct { HANDLE hproc = nullptr; } g;
static HMODULE g_self;
static HANDLE stop_event;
static int restarts;
static void Log(const char *, ...) {}
static void Warn(const char *, ...) {}
static void HostRestart(const char *) {
    ++restarts;
    SetEvent(stop_event);
    if (WaitForSingleObject(g.hproc,5000) != WAIT_OBJECT_0) std::abort();
    CloseHandle(g.hproc); g.hproc = nullptr;
}
#include "feeder_helper_restart.inc"
static bool start_child(const wchar_t *event_name) {
    ResetEvent(stop_event);
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr,exe,MAX_PATH);
    std::wstring command = L"\"" + std::wstring(exe) + L"\" --child " + event_name;
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)) return false;
    CloseHandle(pi.hThread); g.hproc = pi.hProcess;
    return true;
}
int wmain(int argc,wchar_t **argv) {
    if (argc == 3 && !wcscmp(argv[1],L"--child")) {
        HANDLE event = OpenEventW(SYNCHRONIZE,FALSE,argv[2]);
        if (!event) return 20;
        const DWORD result = WaitForSingleObject(event,5000);
        CloseHandle(event); return result == WAIT_OBJECT_0 ? 0 : 21;
    }
    const bool disabled = argc == 2 && !wcscmp(argv[1],L"--disabled");
    SetEnvironmentVariableA("FS_FEEDER_RESTART_ON_RESIZE",disabled ? nullptr : "1");
    SetEnvironmentVariableA("FS_FEEDER_HISTORY_DIR",nullptr);
    g_self = GetModuleHandleW(nullptr);
    wchar_t name[100] = {};
    swprintf_s(name,L"Local\\FSHelperRestartTest-%lu",GetCurrentProcessId());
    stop_event = CreateEventW(nullptr,TRUE,FALSE,name);
    if (!stop_event || !start_child(name)) return 1;
    if (!FsBeforeGlBuild(1920,1111)) return 2;
    FsGlBuildSubmitted(1920,1111);
    if (disabled) {
        if (!FsBeforeGlBuild(1006,691) || restarts) return 3;
        HostRestart("test cleanup");
    } else {
        if (FsGlExtentReady(1920,1111)) return 4;
        Sleep(270);
        if (!FsGlExtentReady(1920,1111) || FsGlExtentReady(1006,691)) return 5;
        // Each requested build must retire the process which already owns a build.
        if (FsBeforeGlBuild(1006,691) || restarts != 1 || g.hproc) return 6;
        if (!start_child(name) || !FsBeforeGlBuild(1006,691)) return 7;
        FsGlBuildSubmitted(1006,691);
        if (FsBeforeGlBuild(1920,1111) || restarts != 2) return 8;
        if (!start_child(name) || !FsBeforeGlBuild(1920,1111)) return 9;
        FsGlBuildSubmitted(1920,1111);
        // A same-size config rebuild also gets a fresh process.
        if (FsBeforeGlBuild(1920,1111) || restarts != 3) return 10;
    }
    CloseHandle(stop_event);
    std::puts("Helper opt-in, extent settling and one-build-per-process policy passed with real child processes (no GPU/NGX exercised).");
    return 0;
}
