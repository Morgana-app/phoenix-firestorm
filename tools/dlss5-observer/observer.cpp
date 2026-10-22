// Scene-boundary observer with an opt-in, bounded P0 local-copy diagnostic.
#include <reshade.hpp>
#include <GL/gl.h>
#include "detection.hpp"
#include "fsscenesignal.h"
#include "copy_probe.hpp"
#include "identity_probe.hpp"
#include "early_neural.hpp"
#include <array>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <unordered_map>

static_assert(RESHADE_API_VERSION == 20, "Build with ReShade 6.8.0 API 20");
namespace api = reshade::api;
namespace
{
struct candidate_record
{
    uint64_t draw_event = 0, boundary_event = 0, following_event = 0;
    uint64_t pipeline = 0, following_pipeline = 0, rtv = 0, dsv = 0;
    uint64_t color_view = 0, depth_view = 0;
    int program = 0, draw_fbo = 0, read_fbo = 0;
    int color_unit = -1, depth_unit = -1;
    std::array<int, 4> viewport = {};
    uint32_t color_width = 0, color_height = 0, depth_width = 0, depth_height = 0;
    LONG client_width = 0, client_height = 0;
    HWND hwnd = nullptr;
    bool following_indexed = false;
};
struct context_state
{
    observer::sequence sequence;
    observer::records<candidate_record, 8> retained;
    observer::totals totals;
    observer::presentation_clock clock;
    api::pipeline program = {};
    bool recognized = false, in_effects = false, triangle_list = false;
    bool sample_present = false, have_viewport = false;
    std::array<int, 4> previous_viewport = {};
    LONG previous_client_width = 0, previous_client_height = 0;
    uint64_t events = 0, candidate_event = 0;
    uint64_t last_signal_serial = 0, signals = 0, eligible_signals = 0, rejected_signals = 0;
    uint64_t uncorrelated_signals = 0;
    uint32_t previous_signal_reasons = UINT32_MAX, signal_detail_logs = 0;
    uint32_t detail_frames = 0, anomaly_logs = 0, viewport_logs = 0;
    api::resource_view rtv = {}, dsv = {};
    std::array<api::resource_view, 128> textures = {};
    bool detailed() const { return clock.interval < 12 || (detail_frames < 8 && sequence.candidates != 0) || clock.interval % 120 == 0; }
    candidate_record *latest()
    {
        if (retained.size && retained.values[retained.size - 1].draw_event == candidate_event)
            return &retained.values[retained.size - 1];
        return nullptr;
    }
};
std::mutex mutex;
std::unordered_map<api::command_list *, context_state> contexts;
std::unordered_map<api::device *, std::unordered_map<uint64_t, uint64_t>> programs;
uint32_t identity_logs = 0;

void log(const char *format, ...)
{
    char buffer[1800];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    reshade::log::message(reshade::log::level::info, buffer);
}
bool is_gl(api::device *device) { return device->get_api() == api::device_api::opengl; }
void dump_candidates(api::command_list *cmd, const context_state &state);
void summary(api::command_list *cmd, const context_state &state, const char *reason)
{
    const auto &t = state.totals;
    log("[FSObserver] run_summary cmd=%p reason=%s intervals=%llu zero=%llu single=%llu multiple=%llu incomplete=%llu candidates=%llu boundaries=%llu tail_candidates=%u tail_boundaries=%u tail_dropped=%llu signals=%llu eligible_signals=%llu rejected_signals=%llu uncorrelated_signals=%llu",
        static_cast<void *>(cmd), reason, t.intervals, t.zero, t.single, t.multiple, t.incomplete,
        t.candidates, t.boundaries, state.sequence.candidates, state.sequence.boundaries, state.retained.dropped,
        state.signals, state.eligible_signals, state.rejected_signals, state.uncorrelated_signals);
    if (state.retained.size) dump_candidates(cmd, state);
}

void init_pipeline(api::device *device, api::pipeline_layout, uint32_t count,
    const api::pipeline_subobject *objects, api::pipeline pipeline)
{
    if (!is_gl(device)) return;
    std::string source;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (objects[i].type != api::pipeline_subobject_type::pixel_shader) continue;
        const auto *shaders = static_cast<const api::shader_desc *>(objects[i].data);
        for (uint32_t j = 0; j < objects[i].count; ++j)
            if (shaders[j].code && shaders[j].code_size)
                source.append(static_cast<const char *>(shaders[j].code), shaders[j].code_size);
    }
    const bool match = observer::matches_final_fragment(source);
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char byte : source) { hash ^= byte; hash *= 1099511628211ull; }
    std::lock_guard lock(mutex);
    // Relinking the same program must invalidate any old recognition.
    auto &known = programs[device];
    known.erase(pipeline.handle);
    if (match)
    {
        known[pipeline.handle] = hash;
        if (identity_logs++ < 24)
            log("[FSObserver] shader_match device=%p pipeline=%llx source_fnv64=%llx bytes=%zu (candidate identity, not frame eligibility)",
                static_cast<void *>(device), pipeline.handle, hash, source.size());
    }
    for (auto &[cmd, state] : contexts)
        if (cmd->get_device() == device && state.program == pipeline)
        {
            state.recognized = match;
            state.sequence.awaiting_unbind = false;
        }
}
void destroy_pipeline(api::device *device, api::pipeline pipeline)
{
    if (!is_gl(device)) return;
    std::lock_guard lock(mutex);
    programs[device].erase(pipeline.handle);
    for (auto &[cmd, state] : contexts)
        if (cmd->get_device() == device && state.program == pipeline)
        {
            state.recognized = false;
            state.sequence.awaiting_unbind = false;
        }
}
void init_command_list(api::command_list *cmd)
{
    if (!is_gl(cmd->get_device())) return;
    std::lock_guard lock(mutex);
    contexts.try_emplace(cmd);
}
void destroy_command_list(api::command_list *cmd)
{
    std::lock_guard lock(mutex);
    if (auto it = contexts.find(cmd); it != contexts.end()) summary(cmd, it->second, "context_destroy");
    contexts.erase(cmd);
}
void destroy_device(api::device *device)
{
    std::lock_guard lock(mutex);
    programs.erase(device);
    for (auto it = contexts.begin(); it != contexts.end();)
        if (it->first->get_device() == device)
        {
            summary(it->first, it->second, "device_destroy");
            it = contexts.erase(it);
        }
        else ++it;
}
void bind_pipeline(api::command_list *cmd, api::pipeline_stage stages, api::pipeline pipeline)
{
    // GL emits all_shader_stages for glUseProgram, input_assembler for VAOs.
    if (stages != api::pipeline_stage::all_shader_stages) return;
    std::lock_guard lock(mutex);
    auto it = contexts.find(cmd);
    if (it == contexts.end() || it->second.in_effects) return;
    auto &state = it->second;
    ++state.events;
    if (state.sequence.program_change(pipeline.handle == 0))
        if (auto *record = state.latest()) record->boundary_event = state.events;
    state.program = pipeline;
    const auto device = programs.find(cmd->get_device());
    state.recognized = device != programs.end() && device->second.count(pipeline.handle) != 0;
}
void bind_targets(api::command_list *cmd, uint32_t count, const api::resource_view *rtvs, api::resource_view dsv)
{
    std::lock_guard lock(mutex);
    auto it = contexts.find(cmd);
    if (it == contexts.end() || it->second.in_effects) return;
    it->second.rtv = count ? rtvs[0] : api::resource_view{};
    it->second.dsv = dsv;
}
void bind_states(api::command_list *cmd, uint32_t count, const api::dynamic_state *states, const uint32_t *values)
{
    std::lock_guard lock(mutex);
    auto it = contexts.find(cmd);
    if (it == contexts.end() || it->second.in_effects) return;
    for (uint32_t i = 0; i < count; ++i)
        if (states[i] == api::dynamic_state::primitive_topology)
            it->second.triangle_list = values[i] == static_cast<uint32_t>(api::primitive_topology::triangle_list);
}
void push_descriptors(api::command_list *cmd, api::shader_stage, api::pipeline_layout,
    uint32_t, const api::descriptor_table_update &update)
{
    if (update.type != api::descriptor_type::sampler_with_resource_view || !update.descriptors) return;
    std::lock_guard lock(mutex);
    auto it = contexts.find(cmd);
    if (it == contexts.end() || it->second.in_effects) return;
    const auto *values = static_cast<const api::sampler_with_resource_view *>(update.descriptors);
    for (uint32_t i = 0; i < update.count && update.binding < 128 && i < 128 - update.binding; ++i)
        it->second.textures[update.binding + i] = values[i].view;
}

// Read-only queries. No glActiveTexture, binds, GL error clearing, or synchronization.
int sampler_unit(GLint program, const char *name)
{
    using location_fn = GLint(APIENTRY *)(GLuint, const char *);
    using uniform_fn = void(APIENTRY *)(GLuint, GLint, GLint *);
    auto location = reinterpret_cast<location_fn>(wglGetProcAddress("glGetUniformLocation"));
    auto uniform = reinterpret_cast<uniform_fn>(wglGetProcAddress("glGetUniformiv"));
    const auto valid = [](auto fn) {
        const auto value = reinterpret_cast<uintptr_t>(fn);
        return value > 3 && value != UINTPTR_MAX;
    };
    if (!valid(location) || !valid(uniform)) return -1;
    const GLint index = location(static_cast<GLuint>(program), name);
    if (index < 0) return -1;
    GLint value = -1;
    uniform(static_cast<GLuint>(program), index, &value);
    return value;
}
void capture_candidate(api::command_list *cmd, context_state &state, candidate_record &record)
{
    record.draw_event = state.events;
    record.pipeline = state.program.handle;
    record.rtv = state.rtv.handle;
    record.dsv = state.dsv.handle;
    glGetIntegerv(0x8B8D /* GL_CURRENT_PROGRAM */, &record.program);
    glGetIntegerv(0x8CA6 /* GL_DRAW_FRAMEBUFFER_BINDING */, &record.draw_fbo);
    glGetIntegerv(0x8CAA /* GL_READ_FRAMEBUFFER_BINDING */, &record.read_fbo);
    glGetIntegerv(GL_VIEWPORT, record.viewport.data());
    record.hwnd = WindowFromDC(wglGetCurrentDC());
    RECT client = {};
    if (record.hwnd && GetClientRect(record.hwnd, &client))
    {
        record.client_width = client.right - client.left;
        record.client_height = client.bottom - client.top;
    }
    record.color_unit = sampler_unit(record.program, "diffuseRect");
    record.depth_unit = sampler_unit(record.program, "depthMap");
    const auto view = [&](int unit) -> uint64_t { return unit >= 0 && unit < 128 ? state.textures[unit].handle : 0; };
    record.color_view = view(record.color_unit);
    record.depth_view = view(record.depth_unit);
    const auto extent = [&](uint64_t handle, uint32_t &width, uint32_t &height) {
        if (!handle) return;
        const auto resource = cmd->get_device()->get_resource_from_view(api::resource_view{handle});
        const auto desc = cmd->get_device()->get_resource_desc(resource);
        if (desc.type == api::resource_type::texture_2d) { width = static_cast<uint32_t>(desc.texture.width); height = desc.texture.height; }
    };
    extent(record.color_view, record.color_width, record.color_height);
    extent(record.depth_view, record.depth_width, record.depth_height);
    if ((!state.have_viewport || state.previous_viewport != record.viewport ||
        state.previous_client_width != record.client_width || state.previous_client_height != record.client_height) && state.viewport_logs++ < 64)
        log("[FSObserver] viewport_change cmd=%p interval=%llu candidate=%u viewport=%d,%d,%d,%d client=%ld,%ld previous=%d,%d,%d,%d previous_client=%ld,%ld first=%d (UI state not inferred)",
            static_cast<void *>(cmd), state.clock.interval, state.sequence.candidates,
            record.viewport[0], record.viewport[1], record.viewport[2], record.viewport[3], record.client_width, record.client_height,
            state.previous_viewport[0], state.previous_viewport[1], state.previous_viewport[2], state.previous_viewport[3],
            state.previous_client_width, state.previous_client_height, state.have_viewport ? 0 : 1);
    state.have_viewport = true;
    state.previous_viewport = record.viewport;
    state.previous_client_width = record.client_width;
    state.previous_client_height = record.client_height;
}
void dump_candidates(api::command_list *cmd, const context_state &state)
{
    for (size_t i = 0; i < state.retained.size; ++i)
    {
        const auto &r = state.retained.values[i];
        log("[FSObserver] candidate_record cmd=%p hwnd=%p interval=%llu ordinal=%zu draw_event=%llu boundary_event=%llu following_event=%llu pipeline=%llx next_pipeline=%llx next_indexed=%d native_program=%d draw_fbo=%d read_fbo=%d rtv=%llx dsv=%llx viewport=%d,%d,%d,%d client=%ld,%ld color_unit=%d color_view=%llx color_extent=%u,%u depth_unit=%d depth_view=%llx depth_extent=%u,%u (retained pre-draw state; not an eligibility verdict)",
            static_cast<void *>(cmd), static_cast<void *>(r.hwnd), state.clock.interval, i + 1,
            r.draw_event, r.boundary_event, r.following_event, r.pipeline, r.following_pipeline, r.following_indexed ? 1 : 0,
            r.program, r.draw_fbo, r.read_fbo, r.rtv, r.dsv, r.viewport[0], r.viewport[1], r.viewport[2], r.viewport[3],
            r.client_width, r.client_height, r.color_unit, r.color_view, r.color_width, r.color_height,
            r.depth_unit, r.depth_view, r.depth_width, r.depth_height);
    }
}
bool observe_draw(api::command_list *cmd, bool triangle, bool indexed)
{
    std::lock_guard lock(mutex);
    auto it = contexts.find(cmd);
    if (it == contexts.end() || it->second.in_effects) return false;
    auto &state = it->second;
    ++state.events;
    if (state.sequence.awaiting_following_draw)
        if (auto *record = state.latest())
        {
            record->following_event = state.events;
            record->following_pipeline = state.program.handle;
            record->following_indexed = indexed;
        }
    const bool candidate = triangle && state.triangle_list && state.recognized;
    state.sequence.draw(candidate);
    if (candidate)
    {
        state.candidate_event = state.events;
        if (auto *record = state.retained.append()) capture_candidate(cmd, state, *record);
    }
    return false; // Always allow the original draw.
}
bool draw(api::command_list *cmd, uint32_t vertices, uint32_t instances, uint32_t first, uint32_t first_instance)
{
    return observe_draw(cmd, vertices == 3 && instances == 1 && first == 0 && first_instance == 0, false);
}
bool draw_indexed(api::command_list *cmd, uint32_t, uint32_t, uint32_t, int32_t, uint32_t)
{
    return observe_draw(cmd, false, true);
}
bool draw_indirect(api::command_list *cmd, api::indirect_command type, api::resource, uint64_t, uint32_t, uint32_t)
{
    if (type == api::indirect_command::draw || type == api::indirect_command::draw_indexed)
        return observe_draw(cmd, false, true);
    return false;
}
void present(api::command_queue *queue, api::swapchain *swapchain, const api::rect *, const api::rect *, uint32_t, const api::rect *)
{
    if (!is_gl(queue->get_device())) return;
    auto *cmd = queue->get_immediate_command_list();
    early_neural::begin_present(cmd);
    if (early_neural::lean_mode()) return;
    std::lock_guard lock(mutex);
    auto it = contexts.find(cmd);
    if (it == contexts.end()) return;
    auto &state = it->second;
    const auto &s = state.sequence;
    const bool anomaly = s.candidates != s.boundaries || s.candidates > 1 || s.interrupted != 0;
    const bool trace = state.detailed() || (anomaly && state.anomaly_logs++ < 24);
    if (trace)
    {
        log("[FSObserver] present cmd=%p swapchain=%p interval=%llu candidates=%u boundaries=%u following_draws=%u interrupted=%u pending_unbind=%d retained=%zu dropped=%llu events=%llu (counts only; no eligibility verdict)",
            static_cast<void *>(cmd), static_cast<void *>(swapchain), state.clock.interval, s.candidates, s.boundaries,
            s.following_draws, s.interrupted, s.awaiting_unbind ? 1 : 0, state.retained.size, state.retained.dropped, state.events);
        dump_candidates(cmd, state);
    }
    if (state.clock.interval == 120 && state.detail_frames == 0 && s.candidates == 0)
        log("[FSObserver] no_candidate_yet cmd=%p; source signature unavailable or not used. Firestorm binary shader cache bypasses source-link events; see observer README. This is not proof the scene boundary is absent.", static_cast<void *>(cmd));
    state.totals.add(s);
    if (s.candidates != 0) ++state.detail_frames;
    state.sequence = {};
    state.retained = {};
    state.candidate_event = 0;
    state.sample_present = trace;
    state.clock.begin_present();
    if (state.totals.intervals % 600 == 0) summary(cmd, state, "periodic");
}
void finish_present(api::command_queue *queue, api::swapchain *)
{
    if (!is_gl(queue->get_device())) return;
    auto *cmd = queue->get_immediate_command_list();
    identity_probe::finish_present(cmd);
    early_neural::finish_present(cmd);
    if (early_neural::lean_mode()) return;
    std::lock_guard lock(mutex);
    if (auto it = contexts.find(cmd); it != contexts.end()) it->second.clock.finish_present();
}
void init_runtime(api::effect_runtime *runtime)
{
    if (!is_gl(runtime->get_device())) return;
    identity_probe::init(runtime);
    early_neural::init(runtime);
    log("[FSObserver] runtime_init runtime=%p device=%p hwnd=%p", static_cast<void *>(runtime),
        static_cast<void *>(runtime->get_device()), runtime->get_hwnd());
}
void destroy_runtime(api::effect_runtime *runtime)
{
    identity_probe::destroy(runtime);
    early_neural::destroy(runtime);
    if (is_gl(runtime->get_device())) log("[FSObserver] runtime_destroy runtime=%p", static_cast<void *>(runtime));
}
void begin_effects(api::effect_runtime *runtime, api::command_list *cmd, api::resource_view rtv, api::resource_view)
{
    identity_probe::begin(runtime);
    std::lock_guard lock(mutex);
    auto it = contexts.find(cmd);
    if (it == contexts.end()) return;
    auto &state = it->second;
    if ((state.clock.in_present && state.sample_present) || (!state.clock.in_present && state.detailed()))
        log("[FSObserver] effects_begin runtime=%p cmd=%p interval=%llu phase=%s rtv=%llx (existing chain; interval is an observation span, not an engine frame ID)",
            static_cast<void *>(runtime), static_cast<void *>(cmd), state.clock.effect_interval(),
            state.clock.in_present ? "present" : "outside_present", rtv.handle);
    state.in_effects = true;
}
void finish_effects(api::effect_runtime *runtime, api::command_list *cmd, api::resource_view, api::resource_view)
{
    identity_probe::end(runtime);
    std::lock_guard lock(mutex);
    auto it = contexts.find(cmd);
    if (it != contexts.end()) it->second.in_effects = false;
}
void reloaded(api::effect_runtime *runtime)
{
    identity_probe::reload(runtime);
    early_neural::reload(runtime);
    if (is_gl(runtime->get_device())) log("[FSObserver] effects_reloaded runtime=%p", static_cast<void *>(runtime));
}

void rendered_technique(api::effect_runtime *runtime, api::effect_technique technique, api::command_list *cmd,
    api::resource_view, api::resource_view)
{
    bool at_present = false;
    {
        std::lock_guard lock(mutex);
        if (const auto it = contexts.find(cmd); it != contexts.end()) at_present = it->second.clock.in_present;
    }
    identity_probe::technique(runtime, technique, at_present);
    early_neural::technique(runtime, technique, at_present);
}

uint32_t receive_scene_signal(const FSSceneReadyV1 *packet)
{
    if (early_neural::lean_mode()) return early_neural::notify(packet);
    if (!packet || !fsSceneCompatible(*packet)) return 0;
    const auto current = reinterpret_cast<uintptr_t>(wglGetCurrentContext());
    if (!current || packet->context_cookie != current) return 0;
    std::unique_lock lock(mutex);
    // Find the live context via its native API identity; do not decode API handles.
    for (auto &[cmd, state] : contexts)
    {
        if (cmd->get_native() != current) continue;
        if (state.in_effects || packet->scene_serial <= state.last_signal_serial) return 0;
        if (packet->rejection_reasons != fsSceneRejections(*packet)) return 0;
        state.last_signal_serial = packet->scene_serial;
        ++state.signals;
        const bool eligible = packet->rejection_reasons == 0;
        if (eligible) ++state.eligible_signals; else ++state.rejected_signals;
        const auto *record = state.latest();
        const bool correlated = record && record->boundary_event != 0 && record->following_event == 0 &&
            record->boundary_event == state.events;
        if (!correlated) ++state.uncorrelated_signals;
        if (state.detailed() || packet->rejection_reasons != state.previous_signal_reasons ||
            (!eligible && state.signal_detail_logs < 32) || !correlated)
        {
            // Hard bound diagnostics even if a future viewer sends a signal storm.
            if (state.signal_detail_logs++ < 256)
                log("[FSObserver] scene_signal cmd=%p interval=%llu display_serial=%llu scene_serial=%llu purpose=%u conditions=%x reasons=%x eligible=%d correlated=%d ordinal=%u event=%llu world=%d,%d,%d,%d viewport=%d,%d,%d,%d client=%d,%d color=%u target=%x extent=%u,%u depth=%u target=%x extent=%u,%u (notification only; no processing)",
                    static_cast<void *>(cmd), state.clock.interval, packet->display_serial, packet->scene_serial,
                    packet->purpose, packet->conditions, packet->rejection_reasons, eligible ? 1 : 0, correlated ? 1 : 0,
                    state.sequence.candidates, state.events, packet->world_rect[0], packet->world_rect[1], packet->world_rect[2], packet->world_rect[3],
                    packet->viewport[0], packet->viewport[1], packet->viewport[2], packet->viewport[3],
                    packet->client_extent[0], packet->client_extent[1], packet->color_texture, packet->color_target,
                    packet->color_extent[0], packet->color_extent[1], packet->depth_texture, packet->depth_target,
                    packet->depth_extent[0], packet->depth_extent[1]);
        }
        state.previous_signal_reasons = packet->rejection_reasons;
        // Native GL calls trigger ReShade callbacks. Never execute the diagnostic
        // with the observer mutex held, and never retain context-state references.
        lock.unlock();
        const auto probe = copy_probe::run(*packet, correlated);
        static thread_local const char *previous_probe_status = nullptr;
        if (probe.attempted || probe.status != previous_probe_status)
            log("[FSObserver] copy_probe status=%s scene_serial=%llu extent=%d,%d attempted=%d stage_equal=%d color_equal=%d depth_equal=%d stencil_equal=%d state_equal=%d rgba_bits=%d,%d,%d,%d samples=%d depth_bits=%d stencil_bits=%d (P0 only; no effects/NGX)",
                probe.status, packet->scene_serial, probe.width, probe.height, probe.attempted ? 1 : 0,
                probe.stage_equal ? 1 : 0, probe.color_equal ? 1 : 0, probe.depth_equal ? 1 : 0,
                probe.stencil_equal ? 1 : 0, probe.state_equal ? 1 : 0, probe.red, probe.green, probe.blue, probe.alpha,
                probe.samples, probe.depth_bits, probe.stencil_bits);
        previous_probe_status = probe.status;
        identity_probe::run(*packet, correlated, cmd);
        early_neural::run(*packet, correlated, cmd);
        return 1; // Notification accepted. P0 result is separate from eligibility.
    }
    return 0;
}

#define OBSERVER_TRACE_EVENTS(X) \
    X(init_pipeline, init_pipeline) X(destroy_pipeline, destroy_pipeline) \
    X(init_command_list, init_command_list) X(destroy_command_list, destroy_command_list) \
    X(destroy_device, destroy_device) X(bind_pipeline, bind_pipeline) \
    X(bind_render_targets_and_depth_stencil, bind_targets) X(bind_pipeline_states, bind_states) X(push_descriptors, push_descriptors) \
    X(draw, draw) X(draw_indexed, draw_indexed) X(draw_or_dispatch_indirect, draw_indirect)
#define OBSERVER_CORE_EVENTS(X) \
    X(present, present) X(finish_present, finish_present) X(init_effect_runtime, init_runtime) X(destroy_effect_runtime, destroy_runtime) \
    X(reshade_begin_effects, begin_effects) X(reshade_finish_effects, finish_effects) \
    X(reshade_reloaded_effects, reloaded) X(reshade_render_technique, rendered_technique)
}
extern "C" __declspec(dllexport) const char *NAME = "Firestorm Scene Observer";
extern "C" __declspec(dllexport) uint32_t FS_SCENE_CALL FirestormSceneReadyV1(const FSSceneReadyV1 *packet)
{
    // C ABI: no C++ exception may unwind into the viewer.
    try { return receive_scene_signal(packet); } catch (...) { return 0; }
}
extern "C" __declspec(dllexport) const char *DESCRIPTION = "Default logging only. Opt-in bounded P0 copy / P1 early identity-effect diagnostics. No direct NGX calls.";
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        if (!reshade::register_addon(module)) return FALSE;
#define REGISTER(event, callback) reshade::register_event<reshade::addon_event::event>(callback);
        OBSERVER_CORE_EVENTS(REGISTER)
        if (!early_neural::lean_mode()) { OBSERVER_TRACE_EVENTS(REGISTER) }
#undef REGISTER
        log("[FSObserver] loaded v0.8 API20; early Kernel/Feed composition; resize-settle/recovery, lean mode and sampled checks; no direct NGX calls");
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
#define UNREGISTER(event, callback) reshade::unregister_event<reshade::addon_event::event>(callback);
        OBSERVER_CORE_EVENTS(UNREGISTER)
        if (!early_neural::lean_mode()) { OBSERVER_TRACE_EVENTS(UNREGISTER) }
#undef UNREGISTER
        reshade::unregister_addon(module);
    }
    return TRUE;
}
