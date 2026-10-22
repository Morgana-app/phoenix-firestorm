#pragma once
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <array>
#include <cstddef>

namespace observer
{
// A source signature, not a runtime numeric program ID. Fail closed on changes.
inline bool matches_final_fragment(std::string_view source)
{
    std::string compact;
    compact.reserve(source.size());
    for (unsigned char c : source)
        if (!std::isspace(c) && c != 0) compact.push_back(static_cast<char>(c));
    return compact.find("#defineHAS_NOISE1") != std::string::npos &&
        compact.find("vec4diff=texture(diffuseRect,vary_fragcoord.xy);") != std::string::npos &&
        compact.find("diff.rgb+=nz*0.003;") != std::string::npos &&
        compact.find("gl_FragDepth=texture(depthMap,vary_fragcoord.xy).r;") != std::string::npos;
}

struct sequence
{
    uint32_t candidates = 0, boundaries = 0, interrupted = 0, following_draws = 0;
    bool awaiting_unbind = false, awaiting_following_draw = false;

    void draw(bool candidate)
    {
        if (awaiting_unbind) { ++interrupted; awaiting_unbind = false; }
        if (awaiting_following_draw) { ++following_draws; awaiting_following_draw = false; }
        if (candidate) { ++candidates; awaiting_unbind = true; }
    }
    bool program_change(bool to_zero)
    {
        if (!awaiting_unbind) return false;
        awaiting_unbind = false;
        if (!to_zero) { ++interrupted; return false; }
        ++boundaries;
        awaiting_following_draw = true;
        return true;
    }
};

// Retain the first records even when an interval turns out to be exceptional.
template <typename T, size_t Capacity> struct records
{
    std::array<T, Capacity> values = {};
    size_t size = 0;
    uint64_t dropped = 0;
    T *append()
    {
        if (size == Capacity) { ++dropped; return nullptr; }
        values[size] = {};
        return &values[size++];
    }
};
struct totals
{
    uint64_t intervals = 0, zero = 0, single = 0, multiple = 0, incomplete = 0;
    uint64_t candidates = 0, boundaries = 0;
    void add(const sequence &s)
    {
        ++intervals;
        if (s.candidates == 0) ++zero;
        else if (s.candidates == 1) ++single;
        else ++multiple;
        if (s.candidates != s.boundaries || s.interrupted || s.awaiting_unbind) ++incomplete;
        candidates += s.candidates;
        boundaries += s.boundaries;
    }
};
struct presentation_clock
{
    uint64_t interval = 0, presenting = 0;
    bool in_present = false;
    void begin_present() { presenting = interval++; in_present = true; }
    void finish_present() { in_present = false; }
    uint64_t effect_interval() const { return in_present ? presenting : interval; }
};
}
