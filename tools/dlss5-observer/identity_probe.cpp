#include "identity_probe.hpp"
#include "copy_probe.hpp"
#include "gl_snapshot.hpp"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace identity_probe {
namespace api = reshade::api;
namespace {
struct runtime_state {
    api::effect_runtime *runtime = nullptr;
    api::command_list *cmd = nullptr;
    bool warmed = false, disabled = false, active = false, pending = false;
    uint64_t calls = 0, successful = 0, early_techniques = 0, present_techniques = 0, early_begins = 0, early_ends = 0;
    uint64_t pending_present_before = 0, duplicate_intervals = 0;
    ULONGLONG retry_after = 0;
    unsigned audit_calls = 0;
    ULONGLONG audit_after = 0;
    uint64_t previous_depth_hash = 0;
};
std::mutex guard;
std::unordered_map<api::effect_runtime *, std::shared_ptr<runtime_state>> runtimes;
bool enabled() {
    char value[8] = {};
    return GetEnvironmentVariableA("FS_SCENE_IDENTITY_PROBE", value, sizeof(value)) == 1 && value[0] == '1';
}
bool world_enabled() {
    char value[8] = {};
    return GetEnvironmentVariableA("FS_SCENE_WORLD_PROBE", value, sizeof(value)) == 1 && value[0] == '1';
}
bool audit_enabled() {
    char value[8] = {};
    return GetEnvironmentVariableA("FS_SCENE_INPUT_AUDIT", value, sizeof(value)) == 1 && value[0] == '1';
}
void message(const char *text) { reshade::log::message(reshade::log::level::info, text); }
void status(const char *text) {
    static thread_local const char *previous = nullptr;
    if (previous != text) {
        char line[180]; std::snprintf(line, sizeof(line), "[FSObserver] identity_probe status=%s (P1 only; Feeder/NGX disabled by test launcher)", text);
        message(line); previous = text;
    }
}
bool identity(api::effect_runtime *rt, api::effect_technique tech) {
    char name[256] = {}, effect[256] = {};
    rt->get_technique_name(tech, name); rt->get_technique_effect_name(tech, effect);
    return std::strcmp(name, "FS_P1Identity") == 0 && std::strcmp(effect, "FS_P1Identity.fx") == 0;
}
struct views {
    api::device *device;
    api::resource_view linear = {}, srgb = {};
    api::resource owned = {};
    ~views() {
        if (srgb != 0 && srgb != linear) device->destroy_resource_view(srgb);
        if (linear != 0) device->destroy_resource_view(linear);
        if (owned != 0) device->destroy_resource(owned);
    }
};
void audit_inputs(const FSSceneReadyV1 &p, bool correlated, api::command_list *cmd) {
    if (!copy_probe::valid_scene(p, correlated) || reinterpret_cast<uintptr_t>(wglGetCurrentContext()) != p.context_cookie) return;
    std::shared_ptr<runtime_state> s;
    {
        std::lock_guard lock(guard);
        for (const auto &[rt, state] : runtimes) {
            (void)rt;
            if (state->cmd == cmd) { if (s) return; s = state; }
        }
        if (!s || s->disabled || s->audit_calls >= 6 || GetTickCount64() < s->audit_after) return;
        ++s->audit_calls; s->audit_after = GetTickCount64() + 2000;
    }
    auto *rt = s->runtime;
    if (rt->get_hwnd() != static_cast<void *>(WindowFromDC(wglGetCurrentDC()))) return;
    const auto before = gl_snapshot::capture();
    if (!before.valid) {
        std::lock_guard lock(guard); s->disabled = true; status("skip_input_audit_state_capability"); return;
    }
    const auto depth = copy_probe::read_depth(p.depth_texture, int(p.depth_extent[0]), int(p.depth_extent[1]));
    unsigned variables = 0;
    rt->enumerate_texture_variables(nullptr, [&](api::effect_runtime *runtime, api::effect_texture_variable variable) {
        char effect[256] = {}, name[256] = {};
        runtime->get_texture_variable_effect_name(variable, effect); runtime->get_texture_variable_name(variable, name);
        if (std::strcmp(effect, "DLSS5_Feed.fx") != 0 && std::strcmp(effect, "lumenite_Kernel.fx") != 0) return;
        const bool selected = std::strstr(name, "DepthBufferTex") || std::strcmp(name, "DLSS5_MV") == 0 ||
            std::strcmp(name, "DLSS5_Depth") == 0 || std::strcmp(name, "DLSS5_Mask") == 0 ||
            std::strstr(name, "tFlow") || std::strstr(name, "tConfidence");
        if (!selected) return;
        api::resource_view srv{}, srgb{}; runtime->get_texture_binding(variable, &srv, &srgb);
        const auto resource = srv != 0 ? runtime->get_device()->get_resource_from_view(srv) : api::resource{};
        const auto desc = resource != 0 ? runtime->get_device()->get_resource_desc(resource) : api::resource_desc{};
        ++variables;
        char line[650]; std::snprintf(line, sizeof(line),
            "[FSObserver] input_texture sample=%u phase=scene_ready_before_effects effect=%s name=%s bound=%d extent=%u,%u format=%u samples=%u matches_world=%d matches_client=%d",
            s->audit_calls, effect, name, resource != 0, desc.texture.width, desc.texture.height,
            unsigned(desc.texture.format), unsigned(desc.texture.samples),
            resource != 0 && desc.texture.width == uint32_t(p.world_rect[2]) && desc.texture.height == uint32_t(p.world_rect[3]),
            resource != 0 && desc.texture.width == uint32_t(p.client_extent[0]) && desc.texture.height == uint32_t(p.client_extent[1]));
        message(line);
    });
    const bool broad = gl_snapshot::equal(before, gl_snapshot::capture());
    char line[850]; std::snprintf(line, sizeof(line),
        "[FSObserver] input_audit sample=%u scene_serial=%llu world=%d,%d,%d,%d client=%d,%d original_depth=%u target=%x extent=%d,%d format=%x status=%s finite=%u/%u min=%.9g max=%.9g hash=%llx changed_from_previous=%d touched_state_equal=%d broad_state_equal=%d texture_records=%u (read-only tiles; no pixel alignment or motion correctness claim)",
        s->audit_calls, p.scene_serial, p.world_rect[0], p.world_rect[1], p.world_rect[2], p.world_rect[3],
        p.client_extent[0], p.client_extent[1], p.depth_texture, p.depth_target, depth.width, depth.height, depth.format,
        depth.status, depth.finite, depth.count, depth.minimum, depth.maximum, depth.hash,
        s->audit_calls > 1 && depth.hash != s->previous_depth_hash, depth.state_equal, broad, variables);
    message(line);
    std::lock_guard lock(guard);
    s->previous_depth_hash = depth.hash;
    if (!broad || !depth.valid) s->disabled = true;
}
}
void init(api::effect_runtime *rt) {
    if ((!enabled() && !audit_enabled()) || rt->get_device()->get_api() != api::device_api::opengl) return;
    auto state = std::make_shared<runtime_state>(); state->runtime = rt;
    state->cmd = rt->get_command_queue()->get_immediate_command_list();
    std::lock_guard lock(guard); runtimes[rt] = std::move(state);
}
void destroy(api::effect_runtime *rt) {
    std::lock_guard lock(guard);
    if (const auto it = runtimes.find(rt); it != runtimes.end()) {
        const auto &s = *it->second;
        char line[350]; std::snprintf(line, sizeof(line),
            "[FSObserver] identity_summary calls=%llu successful=%llu early_techniques=%llu present_techniques=%llu early_begins=%llu early_ends=%llu duplicate_intervals=%llu pending=%d disabled=%d",
            s.calls, s.successful, s.early_techniques, s.present_techniques, s.early_begins, s.early_ends, s.duplicate_intervals, s.pending, s.disabled);
        message(line);
        if (s.audit_calls) {
            char audit_line[180]; std::snprintf(audit_line, sizeof(audit_line),
                "[FSObserver] input_audit_summary samples=%u disabled=%d (no early effects or neural calls)", s.audit_calls, s.disabled);
            message(audit_line);
        }
        runtimes.erase(it);
    }
}
void reload(api::effect_runtime *rt) {
    std::lock_guard lock(guard);
    if (const auto it = runtimes.find(rt); it != runtimes.end()) it->second->warmed = false;
}
void begin(api::effect_runtime *rt) {
    std::lock_guard lock(guard);
    if (const auto it = runtimes.find(rt); it != runtimes.end() && it->second->active) ++it->second->early_begins;
}
void end(api::effect_runtime *rt) {
    std::lock_guard lock(guard);
    if (const auto it = runtimes.find(rt); it != runtimes.end() && it->second->active) ++it->second->early_ends;
}
void technique(api::effect_runtime *rt, api::effect_technique tech, bool present) {
    if (!enabled() || !identity(rt, tech)) return;
    std::lock_guard lock(guard);
    if (const auto it = runtimes.find(rt); it != runtimes.end()) {
        auto &s = *it->second;
        if (s.active) ++s.early_techniques;
        else if (present) { ++s.present_techniques; s.warmed = true; }
    }
}
void finish_present(api::command_list *cmd) {
    std::lock_guard lock(guard);
    for (auto &[rt, entry] : runtimes) {
        (void)rt;
        auto &s = *entry;
        if (s.cmd != cmd || !s.pending) continue;
        const auto duplicate = s.present_techniques - s.pending_present_before;
        if (duplicate) { ++s.duplicate_intervals; s.disabled = true; }
        char line[220]; std::snprintf(line, sizeof(line),
            "[FSObserver] identity_present_check call=%llu identity_at_later_present=%llu suppressed=%d",
            s.calls, duplicate, duplicate == 0);
        message(line); s.pending = false;
    }
}
void run(const FSSceneReadyV1 &p, bool correlated, api::command_list *cmd) {
    if (audit_enabled()) { audit_inputs(p, correlated, cmd); return; }
    if (!enabled()) return;
    const bool world = world_enabled();
    if (!(world ? copy_probe::valid_scene(p, correlated) : copy_probe::full_scene(p, correlated))) { status("skip_scene_or_coverage"); return; }
    if (p.client_extent[0] > 4096 || p.client_extent[1] > 4096) { status("skip_size_limit"); return; }
    std::shared_ptr<runtime_state> s;
    {
        std::lock_guard lock(guard);
        for (const auto &[rt, state] : runtimes) {
            (void)rt;
            if (state->cmd == cmd) { if (s) { status("skip_ambiguous_runtime"); return; } s = state; }
        }
        if (!s) { status("skip_runtime"); return; }
        if (s->disabled || s->active || s->pending) { status("skip_disabled_or_reentrant"); return; }
        if (world ? (s->successful >= 3 || s->calls >= 12) : s->calls >= 3) { status("budget_complete"); return; }
        if (world && GetTickCount64() < s->retry_after) return;
        if (!s->warmed) { status("wait_identity_present"); return; }
    }
    auto *rt = s->runtime;
    if (rt->get_hwnd() != static_cast<void *>(WindowFromDC(wglGetCurrentDC())) || !rt->get_effects_state()) {
        status("skip_window_or_effects_off"); return;
    }
    unsigned count = 0; bool only_identity = true;
    rt->enumerate_techniques(nullptr, [&](api::effect_runtime *runtime, api::effect_technique tech) {
        if (runtime->get_technique_state(tech)) { ++count; only_identity = only_identity && identity(runtime, tech); }
    });
    if (count != 1 || !only_identity) { status("skip_nonidentity_preset"); return; }
    const auto back = rt->get_current_back_buffer();
    const auto desc = rt->get_device()->get_resource_desc(back);
    // GL's native default framebuffer is represented as a surface by API 20.
    if ((desc.type != api::resource_type::surface && desc.type != api::resource_type::texture_2d) ||
        desc.texture.width != uint32_t(p.client_extent[0]) ||
        desc.texture.height != uint32_t(p.client_extent[1]) || desc.texture.samples > 1 || desc.texture.depth_or_layers != 1) {
        status("skip_runtime_extent_or_samples"); return;
    }
    views target{rt->get_device()};
    const auto setup_before = gl_snapshot::capture();
    if (!setup_before.valid) { status("skip_state_snapshot_capability"); return; }
    if (world) {
        const api::resource_desc stage_desc(uint32_t(p.world_rect[2]), uint32_t(p.world_rect[3]), 1, 1,
            api::format::r8g8b8a8_unorm, 1, api::memory_heap::default_,
            api::resource_usage::render_target | api::resource_usage::shader_resource | api::resource_usage::copy_source | api::resource_usage::copy_dest);
        if (!target.device->create_resource(stage_desc, nullptr, api::resource_usage::render_target, &target.owned) ||
            !target.device->create_resource_view(target.owned, api::resource_usage::render_target,
                api::resource_view_desc{api::format::r8g8b8a8_unorm, 0, 1, 0, 1}, &target.linear)) {
            status("skip_world_target"); return;
        }
        target.srgb = target.linear;
        // render_effects selects/queues the matching permutation internally.
        // add_effect_permutation is an internal implementation detail, not API20.
    } else if (!target.device->create_resource_view(back, api::resource_usage::render_target,
        api::resource_view_desc{api::format_to_default_typed(desc.texture.format, 0), 0, 1, 0, 1}, &target.linear) ||
        !target.device->create_resource_view(back, api::resource_usage::render_target,
        api::resource_view_desc{api::format_to_default_typed(desc.texture.format, 1), 0, 1, 0, 1}, &target.srgb)) {
        status("skip_target_views"); return;
    }
    if (!gl_snapshot::equal(setup_before, gl_snapshot::capture())) {
        std::lock_guard lock(guard); s->disabled = true; status("failed_target_setup_state"); return;
    }
    // Refuse unsupported snapshot capabilities before consuming a render_effects
    // once-per-frame slot. This read-only snapshot is repeated around the call.
    if (!gl_snapshot::capture().valid) { status("skip_state_snapshot_capability"); return; }
    struct action_data {
        api::effect_runtime *rt; api::command_list *cmd; views *target;
        runtime_state *state; bool state_equal = false; uint64_t tech_before = 0, begin_before = 0, end_before = 0;
    } data{rt, cmd, &target, s.get()};
    const auto action = [](void *opaque) {
        auto &d = *static_cast<action_data *>(opaque);
        const auto before = gl_snapshot::capture();
        {
            std::lock_guard lock(guard);
            d.tech_before = d.state->early_techniques; d.begin_before = d.state->early_begins; d.end_before = d.state->early_ends;
            d.state->active = true; ++d.state->calls;
            d.state->pending = true; d.state->pending_present_before = d.state->present_techniques;
        }
        // Public 6.8 API captures/restores its app state for calls outside present.
        try {
            d.rt->render_effects(d.cmd, d.target->linear, d.target->srgb);
        } catch (...) {
            std::lock_guard lock(guard);
            d.state->active = false; d.state->disabled = true;
            throw; // The exported observer ABI catches C++ exceptions.
        }
        { std::lock_guard lock(guard); d.state->active = false; }
        d.state_equal = gl_snapshot::equal(before, gl_snapshot::capture());
    };
    const auto bind_stage = [](void *opaque) {
        auto &d = *static_cast<action_data *>(opaque);
        const api::render_pass_render_target_desc attachment{d.target->linear};
        d.cmd->begin_render_pass(1, &attachment, nullptr);
        d.cmd->end_render_pass();
    };
    const auto result = world ? copy_probe::run_world(p, correlated, bind_stage, action, &data) :
        copy_probe::run(p, correlated, action, &data, true);
    if (!result.attempted) { status(result.status); return; }
    std::lock_guard lock(guard);
    const auto rendered = s->early_techniques - data.tech_before;
    const auto began = s->early_begins - data.begin_before, ended = s->early_ends - data.end_before;
    const bool images = result.color_equal && result.depth_equal && result.stencil_equal && result.state_equal && data.state_equal &&
        (world ? (result.stage_equal && result.stage_output_equal && result.outside_equal && result.composited) : result.orientation_corrected);
    const bool pass = images && rendered == 1 && began == 1 && ended == 1;
    const bool warming = world && images && rendered == 0 && began == ended && began <= 1;
    if (warming) s->retry_after = GetTickCount64() + 2000;
    if (pass) ++s->successful;
    if (!pass && !warming) s->disabled = true;
    if (world) {
        char world_line[600]; std::snprintf(world_line, sizeof(world_line),
            "[FSObserver] world_identity status=%s call=%llu successful=%llu world=%d,%d,%d,%d client=%d,%d capture_equal=%d output_equal=%d outside_equal=%d composited=%d native_status=%s",
            pass ? "passed" : warming ? "permutation_pending" : "failed", s->calls, s->successful,
            p.world_rect[0], p.world_rect[1], p.world_rect[2], p.world_rect[3], p.client_extent[0], p.client_extent[1],
            result.stage_equal, result.stage_output_equal, result.outside_equal, result.composited, result.status);
        message(world_line);
    }
    char line[900]; std::snprintf(line, sizeof(line),
        "[FSObserver] identity_probe status=%s call=%llu scene_serial=%llu extent=%d,%d color_equal=%d depth_equal=%d stencil_equal=%d touched_state_equal=%d broad_state_equal=%d identity_techniques=%llu begin=%llu end=%llu rgb_changed=%llu alpha_changed=%llu max_rgb_delta=%u flipped_y_rgb_changed=%llu rollback_equal=%d orientation_corrected=%d raw_rgb_changed=%llu raw_flipped_y_rgb_changed=%llu",
        pass ? "passed" : warming ? "permutation_pending" : "failed", s->calls, p.scene_serial, result.width, result.height,
        result.color_equal, result.depth_equal, result.stencil_equal, result.state_equal, data.state_equal, rendered, began, ended,
        result.changed_rgb_pixels, result.changed_alpha_pixels, result.max_rgb_delta, result.vertical_flip_changed_pixels, result.rollback_equal,
        result.orientation_corrected, result.raw_changed_rgb_pixels, result.raw_vertical_flip_changed_pixels);
    message(line);
}
}
