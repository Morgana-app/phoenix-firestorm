// Test-only receiver, in build/test-receiver. Never install this DLL in Firestorm.
#include "fsscenesignal.h"
extern "C" __declspec(dllexport) uint32_t FS_SCENE_CALL FirestormSceneReadyV1(const FSSceneReadyV1 *packet)
{
    return packet && fsSceneCompatible(*packet) ? 123u : 0u;
}
