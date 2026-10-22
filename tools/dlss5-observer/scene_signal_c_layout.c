#include "fsscenesignal.h"
int main(void)
{
    FSSceneReadyV1 packet = {0};
    packet.struct_size = sizeof(packet);
    packet.abi_version = FS_SCENE_ABI_VERSION;
    return packet.struct_size != 120 || offsetof(FSSceneReadyV1, display_serial) != 8 ||
        offsetof(FSSceneReadyV1, world_rect) != 48 || offsetof(FSSceneReadyV1, color_texture) != 88;
}
