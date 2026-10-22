#include "fsscenesignal.h"
#include <Windows.h>
#include <cstdlib>
#include <iostream>
#include <limits>

static void require(bool value, const char *message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
static FSSceneReadyV1 valid_packet()
{
    FSSceneReadyV1 p = {};
    p.struct_size = sizeof(p); p.abi_version = FS_SCENE_ABI_VERSION;
    p.display_serial = fsSceneContext(FS_SCENE_MAIN).display_serial;
    p.scene_serial = fsNextSceneSerial(); p.context_cookie = 1;
    p.purpose = FS_SCENE_MAIN;
    p.conditions = FS_SCENE_STARTED | FS_SCENE_CONNECTED | FS_SCENE_WORLD_CAMERA | FS_SCENE_PRESENT_INTENDED;
    p.world_rect[2] = p.viewport[2] = p.client_extent[0] = 1920;
    p.world_rect[3] = p.viewport[3] = p.client_extent[1] = 1111;
    p.color_texture = 1; p.depth_texture = 2;
    p.color_target = p.depth_target = 0x0DE1;
    p.color_extent[0] = p.depth_extent[0] = 1920;
    p.color_extent[1] = p.depth_extent[1] = 1111;
    return p;
}
int main(int argc, char **argv)
{
    require(argc == 3, "Test receiver and missing-export fixture paths required");
    auto p = valid_packet();
    require(fsSceneCompatible(p) && fsSceneRejections(p) == 0, "Main scene should be eligible");
    bool requested_snapshot = true;
    const auto original_purpose = fsSceneOriginalPurpose(requested_snapshot);
    requested_snapshot = false; // Actual deferred-snapshot rewrite in display().
    require(original_purpose == FS_SCENE_SNAPSHOT && !requested_snapshot, "Original snapshot purpose must survive rewrite");
    p.purpose = original_purpose;
    require(fsSceneRejections(p) & FS_SCENE_REJECT_PURPOSE, "Snapshot must not become interactive");
    p.purpose = FS_SCENE_UNKNOWN;
    require(fsSceneRejections(p) & FS_SCENE_REJECT_PURPOSE, "Unknown purpose must fail closed");
    p = valid_packet(); p.conditions |= FS_SCENE_QUITTING;
    require(fsSceneRejections(p) & FS_SCENE_REJECT_QUIT, "Quit must exclude processing");
    p.conditions &= ~FS_SCENE_QUITTING;
    require(fsSceneRejections(p) == 0, "Quit cancellation must not latch exclusion");
    p.conditions &= ~FS_SCENE_PRESENT_INTENDED;
    require(fsSceneRejections(p) & FS_SCENE_REJECT_PRESENT, "Swap-suppressed redraw must not qualify as an interactive output");
    p.conditions |= FS_SCENE_PRESENT_INTENDED;
    p.conditions |= 0x80000000u;
    require(fsSceneRejections(p) & FS_SCENE_REJECT_AUXILIARY, "Unknown condition bits must fail closed");
    p = valid_packet();
    p.viewport[2] = p.world_rect[2] = 1921;
    p.color_extent[0] = p.depth_extent[0] = 1921;
    require(fsSceneRejections(p) & FS_SCENE_REJECT_GEOMETRY, "Oversized viewport must be rejected");
    p.viewport[0] = p.world_rect[0] = std::numeric_limits<int32_t>::max();
    require(fsSceneRejections(p) & FS_SCENE_REJECT_GEOMETRY, "Rectangle arithmetic must not overflow");
    p = valid_packet(); p.viewport[3] = p.world_rect[3] = 1088;
    p.color_extent[1] = p.depth_extent[1] = 1088;
    require(fsSceneRejections(p) == 0, "Valid partial world is a scene; processing coverage is a later gate");
    p.depth_extent[1] = 1111;
    require(fsSceneRejections(p) & FS_SCENE_REJECT_GEOMETRY, "Mismatched original-depth extent must be rejected");
    p = valid_packet(); p.abi_version = 99;
    require(!fsSceneCompatible(p) && fsDispatchSceneReady(p) == 0, "Incompatible ABI must not dispatch");
    p = valid_packet(); p.struct_size -= 4;
    require(!fsSceneCompatible(p), "Incompatible size must fail closed");
    p = valid_packet();
    require(!fsSceneSignalAvailable() && fsDispatchSceneReady(p) == 0, "Absent observer must be inert and not loaded");
    const auto module = LoadLibraryA(argv[1]); // Explicit test fixture, not the production dispatcher.
    require(module != nullptr && fsSceneSignalAvailable(), "Test receiver did not load");
    require(fsDispatchSceneReady(p) == 123, "Real dispatcher did not reach the already-loaded export");
    require(FreeLibrary(module) != 0 && !fsSceneSignalAvailable(), "Dispatcher leaked a module reference");
    require(fsDispatchSceneReady(p) == 0, "Unloaded observer must not leave a cached callback");
    const auto old_module = LoadLibraryA(argv[2]);
    require(old_module != nullptr && fsSceneSignalAvailable(), "Missing-export fixture did not load");
    require(fsDispatchSceneReady(p) == 0, "An old observer without the export must be inert");
    require(FreeLibrary(old_module) != 0 && !fsSceneSignalAvailable(), "Missing-export dispatch leaked a module reference");
    const auto c = fsSceneContext(FS_SCENE_MAIN);
    require(fsSceneContext(FS_SCENE_MAIN).display_serial > c.display_serial && fsNextSceneSerial() > p.scene_serial,
        "Render serials must be monotonic");
    std::cout << "Scene ABI, purpose, quit, geometry, serial and real Windows optional-dispatch tests passed\n";
}
