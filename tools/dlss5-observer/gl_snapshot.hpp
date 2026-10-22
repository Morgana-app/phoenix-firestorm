#pragma once
#include <vector>
#include <stdint.h>
namespace gl_snapshot {
// Read-only snapshot of the state covered by ReShade 6.8's GL state block,
// plus pixel-store/PBO state. Deliberately limited to core GL 4.5+.
struct snapshot { bool valid = false; std::vector<int64_t> values; };
snapshot capture();
inline bool equal(const snapshot &a, const snapshot &b) { return a.valid && b.valid && a.values == b.values; }
}
