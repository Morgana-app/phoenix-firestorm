// SPDX-License-Identifier: LGPL-2.1-only
/** Optional, notification-only scene boundary ABI. No ReShade/NGX dependency. */
#ifndef FS_SCENE_SIGNAL_H
#define FS_SCENE_SIGNAL_H
#include <stdint.h>
#include <stddef.h>

#define FS_SCENE_ABI_VERSION 1u
#define FS_SCENE_OBSERVER_MODULE L"firestorm_scene_observer.addon64"
#if defined(_WIN32)
#define FS_SCENE_CALL __stdcall
#else
#define FS_SCENE_CALL
#endif

enum FSScenePurpose
{
    FS_SCENE_UNKNOWN = 0, FS_SCENE_MAIN = 1, FS_SCENE_SNAPSHOT = 2, FS_SCENE_DISCONNECTED = 3
};
enum FSSceneConditions
{
    FS_SCENE_STARTED = 1u << 0, FS_SCENE_CONNECTED = 1u << 1, FS_SCENE_WORLD_CAMERA = 1u << 2,
    FS_SCENE_QUITTING = 1u << 3, FS_SCENE_DISCONNECTING = 1u << 4,
    FS_SCENE_CAPTURE_FRAME = 1u << 5, FS_SCENE_BUFFER_DEBUG = 1u << 6,
    FS_SCENE_CUBE = 1u << 7, FS_SCENE_RESTORE_GL = 1u << 8,
    FS_SCENE_PRESENT_INTENDED = 1u << 9
};
enum FSSceneRejections
{
    FS_SCENE_REJECT_PURPOSE = 1u << 0, FS_SCENE_REJECT_STARTUP = 1u << 1,
    FS_SCENE_REJECT_CONNECTION = 1u << 2, FS_SCENE_REJECT_CAMERA = 1u << 3,
    FS_SCENE_REJECT_QUIT = 1u << 4, FS_SCENE_REJECT_DISCONNECT = 1u << 5,
    FS_SCENE_REJECT_OVERLAY = 1u << 6, FS_SCENE_REJECT_AUXILIARY = 1u << 7,
    FS_SCENE_REJECT_RESOURCES = 1u << 8, FS_SCENE_REJECT_GEOMETRY = 1u << 9,
    FS_SCENE_REJECT_CONTEXT = 1u << 10, FS_SCENE_REJECT_PRESENT = 1u << 11
};

#pragma pack(push, 8)
typedef struct FSSceneReadyV1
{
    uint32_t struct_size, abi_version;
    uint64_t display_serial, scene_serial, context_cookie;
    uint32_t purpose, conditions, rejection_reasons, reserved;
    int32_t world_rect[4], viewport[4], client_extent[2]; // lower-left x,y,width,height
    uint32_t color_texture, color_target, color_extent[2]; // post-process source, pre-noise
    uint32_t depth_texture, depth_target, depth_extent[2]; // original scene depth
} FSSceneReadyV1;
#pragma pack(pop)
typedef uint32_t (FS_SCENE_CALL *FSSceneReadyCallback)(const FSSceneReadyV1 *packet);

#ifdef __cplusplus
static_assert(sizeof(FSSceneReadyV1) == 120, "Scene ABI size changed");
static_assert(offsetof(FSSceneReadyV1, display_serial) == 8 && offsetof(FSSceneReadyV1, world_rect) == 48 &&
    offsetof(FSSceneReadyV1, color_texture) == 88, "Scene ABI layout changed");

struct FSSceneRenderContext { uint64_t display_serial; uint32_t purpose; };
inline uint32_t fsSceneOriginalPurpose(bool requested_snapshot)
{
    return requested_snapshot ? FS_SCENE_SNAPSHOT : FS_SCENE_MAIN;
}
inline bool fsSceneCompatible(const FSSceneReadyV1 &p)
{
    return p.abi_version == FS_SCENE_ABI_VERSION && p.struct_size == sizeof(FSSceneReadyV1) && p.reserved == 0;
}
inline uint32_t fsSceneRejections(const FSSceneReadyV1 &p)
{
    uint32_t reasons = 0;
    constexpr uint32_t known_conditions = FS_SCENE_STARTED | FS_SCENE_CONNECTED | FS_SCENE_WORLD_CAMERA |
        FS_SCENE_QUITTING | FS_SCENE_DISCONNECTING | FS_SCENE_CAPTURE_FRAME | FS_SCENE_BUFFER_DEBUG |
        FS_SCENE_CUBE | FS_SCENE_RESTORE_GL | FS_SCENE_PRESENT_INTENDED;
    if (p.conditions & ~known_conditions) reasons |= FS_SCENE_REJECT_AUXILIARY;
    if (p.purpose != FS_SCENE_MAIN) reasons |= FS_SCENE_REJECT_PURPOSE;
    if (!(p.conditions & FS_SCENE_STARTED)) reasons |= FS_SCENE_REJECT_STARTUP;
    if (!(p.conditions & FS_SCENE_CONNECTED)) reasons |= FS_SCENE_REJECT_CONNECTION;
    if (!(p.conditions & FS_SCENE_WORLD_CAMERA)) reasons |= FS_SCENE_REJECT_CAMERA;
    if (!(p.conditions & FS_SCENE_PRESENT_INTENDED)) reasons |= FS_SCENE_REJECT_PRESENT;
    if (p.conditions & FS_SCENE_QUITTING) reasons |= FS_SCENE_REJECT_QUIT;
    if (p.conditions & FS_SCENE_DISCONNECTING) reasons |= FS_SCENE_REJECT_DISCONNECT;
    if (p.conditions & (FS_SCENE_CAPTURE_FRAME | FS_SCENE_BUFFER_DEBUG)) reasons |= FS_SCENE_REJECT_OVERLAY;
    if (p.conditions & (FS_SCENE_CUBE | FS_SCENE_RESTORE_GL)) reasons |= FS_SCENE_REJECT_AUXILIARY;
    if (!p.context_cookie || !p.scene_serial || !p.display_serial) reasons |= FS_SCENE_REJECT_CONTEXT;
    const auto valid_target = [](uint32_t target) { return target == 0x0DE1u || target == 0x84F5u; }; // 2D / rectangle
    if (!p.color_texture || !p.depth_texture || !valid_target(p.color_target) || !valid_target(p.depth_target) ||
        !p.color_extent[0] || !p.color_extent[1] || !p.depth_extent[0] || !p.depth_extent[1])
        reasons |= FS_SCENE_REJECT_RESOURCES;
    bool geometry = p.client_extent[0] > 0 && p.client_extent[1] > 0;
    for (int i = 0; i < 4; ++i) geometry = geometry && p.world_rect[i] == p.viewport[i];
    geometry = geometry && p.viewport[0] >= 0 && p.viewport[1] >= 0 && p.viewport[2] > 0 && p.viewport[3] > 0 &&
        int64_t(p.viewport[0]) + p.viewport[2] <= p.client_extent[0] &&
        int64_t(p.viewport[1]) + p.viewport[3] <= p.client_extent[1] &&
        int64_t(p.color_extent[0]) == p.viewport[2] && int64_t(p.color_extent[1]) == p.viewport[3] &&
        int64_t(p.depth_extent[0]) == p.viewport[2] && int64_t(p.depth_extent[1]) == p.viewport[3];
    if (!geometry) reasons |= FS_SCENE_REJECT_GEOMETRY;
    return reasons;
}

FSSceneRenderContext fsSceneContext(uint32_t purpose);
uint64_t fsNextSceneSerial();
bool fsSceneSignalAvailable();
uint32_t fsDispatchSceneReady(const FSSceneReadyV1 &packet);
#endif
#endif
