// Local opt-in adaptation for Feeder v1.19.0 (3c09eab), included after HostRestart.
// Called on the render thread under Feeder's existing feed lock.
static DWORD fs_built_host_pid;
static UINT fs_built_width, fs_built_height;
static unsigned fs_retired_hosts;

static bool FsRestartEnabled()
{
    static const bool enabled = [] {
        char value[8] = {};
        return GetEnvironmentVariableA("FS_FEEDER_RESTART_ON_RESIZE", value, sizeof(value)) == 1 && value[0] == '1';
    }();
    return enabled;
}

static bool FsGlExtentReady(UINT w, UINT h)
{
    static early_neural::extent_gate gate;
    return !FsRestartEnabled() || gate.observe(w, h, GetTickCount64());
}

static void FsArchiveRetiredHost(DWORD pid)
{
    char parent[MAX_PATH] = {}, destination[MAX_PATH] = {}, module[MAX_PATH] = {};
    const DWORD length = GetEnvironmentVariableA("FS_FEEDER_HISTORY_DIR", parent, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    if (sprintf_s(destination, "%s\\host-%03u-pid-%lu", parent, ++fs_retired_hosts, pid) < 0) return;
    if (!CreateDirectoryA(destination, nullptr)) { Warn("[fs-resize] cannot create host history (%lu)", GetLastError()); return; }
    GetModuleFileNameA(g_self, module, MAX_PATH);
    char *slash = strrchr(module, '\\');
    if (!slash) return;
    *slash = '\0';
    for (const char *name : {"ReShade.log", "dlss5-feed-host.log", "RenoDX-DLSS5-crash.log", "ReShade.ini", "nvngx.log", "nvsdk_ngx.log", "nvngx_dlss_310_9_16.log"})
    {
        char source[MAX_PATH] = {}, target[MAX_PATH] = {};
        if (sprintf_s(source, "%s\\host64\\%s", module, name) < 0 || sprintf_s(target, "%s\\%s", destination, name) < 0) continue;
        if (!CopyFileA(source, target, TRUE) && GetLastError() != ERROR_FILE_NOT_FOUND)
            Warn("[fs-resize] could not archive %s (%lu)", name, GetLastError());
    }
    Log("[fs-resize] retired host %lu logs: %s", pid, destination);
}

// Called only after consuming any completed build and connecting to a live host.
// A pending build is not interrupted; FeedFrameGl rejects a stale-size result.
static bool FsBeforeGlBuild(UINT w, UINT h)
{
    if (!FsRestartEnabled()) return true;
    const DWORD pid = g.hproc != nullptr ? GetProcessId(g.hproc) : 0;
    if (pid && pid == fs_built_host_pid)
    {
        Log("[fs-resize] restarting host %lu before another build: %ux%u -> %ux%u", pid, fs_built_width, fs_built_height, w, h);
        HostRestart("[fs-resize] fresh consumer process required for the next GL build");
        FsArchiveRetiredHost(pid); // before the replacement truncates the host logs
        fs_built_host_pid = 0;
        return false;
    }
    return true;
}

static void FsGlBuildSubmitted(UINT w, UINT h)
{
    if (!FsRestartEnabled()) return;
    fs_built_host_pid = g.hproc != nullptr ? GetProcessId(g.hproc) : 0;
    fs_built_width = w; fs_built_height = h;
    Log("[fs-resize] first GL build in host %lu: %ux%u", fs_built_host_pid, w, h);
}
