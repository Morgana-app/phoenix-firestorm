#include "detection.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdlib>

static void require(bool condition, const char *message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main()
{
    std::ifstream file(std::string(OBSERVER_SOURCE_ROOT) + "/indra/newview/app_settings/shaders/class1/deferred/postDeferredNoDoFF.glsl");
    require(file.good(), "Cannot read actual Firestorm shader fixture");
    std::string shader{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    require(!observer::matches_final_fragment(shader), "No-noise variant must not match");
    shader = "#define HAS_NOISE 1\n" + shader;
    require(observer::matches_final_fragment(shader), "Actual noise shader must match");
    auto changed = shader;
    changed.replace(changed.find("0.003"), 5, "0.004");
    require(!observer::matches_final_fragment(changed), "Changed shader must fail closed");
    require(!observer::matches_final_fragment("gl_FragDepth = texture(depthMap, vary_fragcoord.xy).r;"), "Depth-writing shader alone is insufficient");
    observer::sequence state;
    require(!state.program_change(true), "Unrelated zero program must not produce boundary");
    state.draw(true);
    require(state.boundaries == 0, "Pre-draw callback must not establish boundary");
    require(state.program_change(true), "Following program unbind must establish boundary");
    state.draw(false);
    require(state.boundaries == 1 && state.following_draws == 1, "Following draw correlation failed");
    state.draw(true); state.draw(false);
    require(!state.program_change(true), "Intervening draw must invalidate pending candidate");
    state.draw(true);
    require(!state.program_change(false), "Switch directly to another program must fail closed");
    state.draw(true); state.program_change(true);
    require(state.boundaries == 2, "Duplicate boundaries must remain visible in counters");
    state = {};
    require(!state.awaiting_unbind && state.boundaries == 0, "Frame reset must discard stale candidate");
    observer::records<int, 4> retained;
    for (int i = 0; i < 4; ++i) *retained.append() = i + 1;
    require(retained.append() == nullptr && retained.dropped == 1, "Record overflow must be explicit");
    require(retained.size == 4 && retained.values[0] == 1 && retained.values[3] == 4, "Extra draws must not overwrite earlier evidence");
    retained = {};
    require(retained.size == 0 && retained.dropped == 0, "Interval records must reset");
    observer::totals totals;
    totals.add(state);
    state.draw(true); state.program_change(true); totals.add(state);
    state.draw(true); state.program_change(true); totals.add(state);
    state.draw(true); totals.add(state);
    require(totals.intervals == 4 && totals.zero == 1 && totals.single == 1 && totals.multiple == 2 && totals.incomplete == 1,
        "Whole-run histogram must count ordinary, multiple and incomplete intervals");
    observer::presentation_clock clock;
    require(clock.effect_interval() == 0 && !clock.in_present, "Early effects must use current observation interval");
    clock.begin_present();
    require(clock.interval == 1 && clock.effect_interval() == 0 && clock.in_present, "Present-time effects must refer to the image just presented");
    clock.finish_present();
    require(clock.effect_interval() == 1 && !clock.in_present, "Next scene must use the next interval");
    clock.begin_present();
    require(clock.effect_interval() == 1, "Presentation labels must advance once");
    std::cout << "Shader identity, event order, bounded anomaly retention, whole-run counts and presentation labels passed\n";
}
