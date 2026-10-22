#include "resize_policy.hpp"
#include <cstdio>
int main() {
    early_neural::extent_gate gate;
    if (gate.observe(1920,1111,100) || gate.observe(1920,1111,349) || !gate.observe(1920,1111,350)) return 1;
    if (gate.observe(1200,800,400) || gate.observe(1210,810,500) || gate.observe(1210,810,749) || !gate.observe(1210,810,750)) return 2;
    if (gate.observe(0,0,800) || gate.observe(1210,810,900) || !gate.observe(1210,810,1150)) return 3;
    if (gate.observe(5000,810,1200) || gate.observe(1920,1111,1300) || !gate.observe(1920,1111,1550)) return 4;
    if (gate.observe(1920,1111,1000)) return 5;
    std::puts("Stable resize, repeated drag, minimize/restore, oversize and clock rollback gates passed.");
    return 0;
}
