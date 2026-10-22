#pragma once
#include <cstdint>
namespace early_neural {
// Avoid reallocating while the user is dragging a window or the runtime and
// native client are temporarily reporting different sizes.
struct extent_gate {
    unsigned width = 0, height = 0;
    uint64_t changed_at = 0;
    bool observe(unsigned w, unsigned h, uint64_t now) {
        if (!w || !h || w > 4096 || h > 4096) { width = height = 0; changed_at = now; return false; }
        if (w != width || h != height) { width = w; height = h; changed_at = now; return false; }
        return now >= changed_at && now - changed_at >= 250;
    }
};
}
