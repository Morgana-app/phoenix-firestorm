// SPDX-License-Identifier: LGPL-2.1-only
// The standalone transport tests compile this file without the viewer's PCH.
#ifndef FS_SCENE_SIGNAL_STANDALONE
#include "llviewerprecompiledheaders.h"
#endif
#include "fsscenesignal.h"
#include <atomic>
#if defined(_WIN32)
#include <Windows.h>
#endif

FSSceneRenderContext fsSceneContext(uint32_t purpose)
{
    static std::atomic<uint64_t> serial{0};
    return {++serial, purpose};
}
uint64_t fsNextSceneSerial()
{
    static std::atomic<uint64_t> serial{0};
    return ++serial;
}
bool fsSceneSignalAvailable()
{
#if defined(_WIN32)
    // This is only an availability check; dispatch retains the module separately.
    return GetModuleHandleW(FS_SCENE_OBSERVER_MODULE) != nullptr;
#else
    return false;
#endif
}
uint32_t fsDispatchSceneReady(const FSSceneReadyV1 &packet)
{
    if (!fsSceneCompatible(packet)) return 0;
#if defined(_WIN32)
    HMODULE module = nullptr;
    // No LoadLibrary: absent add-on is inert. Hold a reference through the call.
    if (!GetModuleHandleExW(0, FS_SCENE_OBSERVER_MODULE, &module)) return 0;
    const auto callback = reinterpret_cast<FSSceneReadyCallback>(GetProcAddress(module, "FirestormSceneReadyV1"));
    const uint32_t result = callback ? callback(&packet) : 0;
    FreeLibrary(module);
    return result;
#else
    return 0;
#endif
}
