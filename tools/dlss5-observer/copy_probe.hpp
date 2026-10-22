#pragma once
#include "fsscenesignal.h"
#include <stdint.h>
#include <vector>
#include <array>

namespace copy_probe {
inline bool valid_scene(const FSSceneReadyV1 &p, bool correlated) {
    return correlated && fsSceneCompatible(p) && fsSceneRejections(p) == 0;
}
inline bool full_scene(const FSSceneReadyV1 &p, bool correlated)
{
    return valid_scene(p, correlated) &&
        p.world_rect[0] == 0 && p.world_rect[1] == 0 &&
        p.world_rect[2] == p.client_extent[0] && p.world_rect[3] == p.client_extent[1];
}
struct result {
    const char *status = "disabled";
    bool attempted = false, color_equal = false, stage_equal = false;
    bool depth_equal = false, stencil_equal = false, state_equal = false;
    int width = 0, height = 0;
    int red = 0, green = 0, blue = 0, alpha = 0, samples = 0, depth_bits = 0, stencil_bits = 0;
    uint64_t changed_rgb_pixels = 0, changed_alpha_pixels = 0, vertical_flip_changed_pixels = 0;
    unsigned max_rgb_delta = 0;
    bool rollback_equal = false;
    bool orientation_corrected = false;
    uint64_t raw_changed_rgb_pixels = 0, raw_vertical_flip_changed_pixels = 0;
    bool stage_output_equal = false, outside_equal = false, composited = false;
};
struct depth_readback {
    const char *status = "unsupported";
    bool valid = false, state_equal = false;
    int width = 0, height = 0, format = 0;
    unsigned finite = 0, count = 0;
    float minimum = 0, maximum = 0;
    uint64_t hash = 0;
    std::array<float, 3 * 32 * 32> values{};
};
// Read-only DSA queries/readback of three depth tiles; no resource-handle decoding.
depth_readback read_depth(uint32_t texture, int expected_width, int expected_height);
// Experimental steady-frame GPU path. Both supplied targets are client-sized.
// execute returns true only for a completed chain with restored API state.
// No CPU readbacks or persistent native objects are created here.
bool process_world(const FSSceneReadyV1 &p, void (*bind_stage)(void *), void (*bind_backup)(void *),
    bool (*execute)(void *), void *data);
// Diagnostic only: opt-in environment switch, at most three copies per process.
// Own GL objects are created/deleted within the synchronous call, never retained.
result run(const FSSceneReadyV1 &packet, bool correlated, void (*action)(void *) = nullptr, void *data = nullptr,
    bool correct_default_orientation = false);
void measure_color(const std::vector<unsigned char> &before, const std::vector<unsigned char> &after, result &out);
// bind_stage uses the supported API to bind an owned world-sized render target.
// The native FBO identity is queried from GL, never decoded from an API handle.
result run_world(const FSSceneReadyV1 &packet, bool correlated, void (*bind_stage)(void *),
    void (*execute)(void *), void *data);
#ifdef FS_PROBE_TEST_HOOKS
bool test_flip_default(int width, int height);
#endif
}
