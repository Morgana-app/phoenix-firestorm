#include "early_neural.hpp"
#include "copy_probe.hpp"
#include "gl_snapshot.hpp"
#include "resize_policy.hpp"
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>

namespace early_neural {
namespace api = reshade::api;
namespace {
using clock = std::chrono::steady_clock;
double milliseconds(clock::duration value) { return std::chrono::duration<double, std::milli>(value).count(); }
bool full_checks() {
    char value[8] = {};
    return GetEnvironmentVariableA("FS_SCENE_EARLY_FULL_CHECKS", value, sizeof(value)) == 1 && value[0] == '1';
}
bool enabled() {
    char value[8] = {};
    return GetEnvironmentVariableA("FS_SCENE_EARLY_NEURAL", value, sizeof(value)) == 1 && value[0] == '1';
}
void log(const char *text) { reshade::log::message(reshade::log::level::info, text); }
int kind(api::effect_runtime *rt, api::effect_technique tech) {
    char name[256] = {}, effect[256] = {};
    rt->get_technique_name(tech, name); rt->get_technique_effect_name(tech, effect);
    if (!std::strcmp(name, "Lumenite_Kernel") && !std::strcmp(effect, "lumenite_Kernel.fx")) return 1;
    if (!std::strcmp(name, "DLSS5_Feed") && !std::strcmp(effect, "DLSS5_Feed.fx")) return 2;
    return 0;
}
struct target {
    api::device *device = nullptr;
    api::resource resource{};
    api::resource_view view{};
    ~target() { if (view != 0) device->destroy_resource_view(view); if (resource != 0) device->destroy_resource(resource); }
    bool create(api::device *d, unsigned w, unsigned h) {
        device = d;
        return d->create_resource(api::resource_desc(w, h, 1, 1, api::format::r8g8b8a8_unorm, 1,
            api::memory_heap::default_, api::resource_usage::render_target | api::resource_usage::shader_resource |
            api::resource_usage::copy_source | api::resource_usage::copy_dest), nullptr,
            api::resource_usage::render_target, &resource) && d->create_resource_view(resource, api::resource_usage::render_target,
                api::resource_view_desc{api::format::r8g8b8a8_unorm, 0, 1, 0, 1}, &view);
    }
};
struct state {
    api::effect_runtime *rt = nullptr;
    api::command_list *cmd = nullptr;
    std::unique_ptr<target> scene, backup;
    unsigned width = 0, height = 0;
    bool active = false, pending = false, disabled = false, ready_kernel = false, ready_feed = false;
    uint64_t kernel = 0, feed = 0, present_feed = 0, pending_before = 0, calls = 0, composed = 0, duplicates = 0;
    uint64_t call_kernel = 0, call_feed = 0;
    ULONGLONG retry_after = 0;
    bool check_next = true;
    bool in_present = false;
    uint64_t last_signal = 0, signals = 0, rejected = 0;
    uint64_t checked = 0, timed = 0;
    double prep_ms = 0, effects_ms = 0, compose_ms = 0, max_ms = 0;
    extent_gate extent;
    bool waiting_guides = false;
    uint64_t target_generations = 0;
};
void timings(const state &s) {
    if (!s.timed) return;
    char line[400]; std::snprintf(line, sizeof(line),
        "[FSObserver] early_neural_timing frames=%llu broad_checked_frames=%llu elapsed_ms_avg[prepare=%.3f effects_call=%.3f compose=%.3f total=%.3f] total_ms_max=%.3f (CPU wall time includes driver waits; not GPU timings)",
        s.timed, s.checked, s.prep_ms / s.timed, s.effects_ms / s.timed, s.compose_ms / s.timed,
        (s.prep_ms + s.effects_ms + s.compose_ms) / s.timed, s.max_ms); log(line);
}
std::mutex guard;
std::unordered_map<api::effect_runtime *, std::shared_ptr<state>> runtimes;
}
bool lean_mode() {
    static const bool result = [] {
        char trace[8] = {};
        return enabled() && !(GetEnvironmentVariableA("FS_SCENE_EARLY_TRACE", trace, sizeof(trace)) == 1 && trace[0] == '1');
    }();
    return result;
}
uint32_t notify(const FSSceneReadyV1 *p) {
    if (!p || !fsSceneCompatible(*p) || p->context_cookie != reinterpret_cast<uintptr_t>(wglGetCurrentContext()) ||
        !p->context_cookie || p->rejection_reasons != fsSceneRejections(*p)) return 0;
    std::shared_ptr<state> s;
    {
        std::lock_guard lock(guard);
        for (const auto &[rt, candidate] : runtimes) {
            (void)rt;
            if (candidate->cmd->get_native() == p->context_cookie) { if (s) return 0; s = candidate; }
        }
        if (!s || s->active || s->in_present || p->scene_serial <= s->last_signal) return 0;
        s->last_signal = p->scene_serial; ++s->signals;
        if (p->rejection_reasons) { ++s->rejected; return 1; }
    }
    // The modified viewer supplies the proven pre-overlay seam. Validate its
    // ABI/context/serial/eligibility instead of rediscovering it on every draw.
    run(*p, true, s->cmd);
    return 1;
}
void begin_present(api::command_list *cmd) {
    std::lock_guard lock(guard);
    for (auto &[rt, entry] : runtimes) { (void)rt; if (entry->cmd == cmd) entry->in_present = true; }
}
void init(api::effect_runtime *rt) {
    if (!enabled() || rt->get_device()->get_api() != api::device_api::opengl) return;
    auto s = std::make_shared<state>(); s->rt = rt; s->cmd = rt->get_command_queue()->get_immediate_command_list();
    log(lean_mode() ? "[FSObserver] early_neural armed=1 signal_only=1 per_draw_trace=0 waiting_for_kernel_feed_present" :
        "[FSObserver] early_neural armed=1 signal_only=0 per_draw_trace=1 waiting_for_kernel_feed_present");
    std::lock_guard lock(guard); runtimes[rt] = std::move(s);
}
void destroy(api::effect_runtime *rt) {
    std::shared_ptr<state> s;
    { std::lock_guard lock(guard); const auto it = runtimes.find(rt); if (it == runtimes.end()) return; s = it->second; runtimes.erase(it); }
    char line[360]; std::snprintf(line, sizeof(line),
        "[FSObserver] early_neural_summary calls=%llu composed=%llu early_kernel=%llu early_feed=%llu duplicates=%llu pending=%d disabled=%d",
        s->calls, s->composed, s->kernel, s->feed, s->duplicates, s->pending, s->disabled); log(line);
    timings(*s);
    if (lean_mode()) {
        char signal_line[200]; std::snprintf(signal_line, sizeof(signal_line),
            "[FSObserver] early_neural_signal_summary accepted=%llu rejected_scenes=%llu last_serial=%llu (explicit seam; no shader correlation tracing)",
            s->signals, s->rejected, s->last_signal); log(signal_line);
    }
    char target_line[180]; std::snprintf(target_line, sizeof(target_line),
        "[FSObserver] early_neural_targets generations=%llu last_extent=%u,%u (owned targets released at runtime destruction)",
        s->target_generations, s->width, s->height); log(target_line);
    // Resource destruction can call other add-ons. Do not hold our mutex here.
}
void reload(api::effect_runtime *rt) {
    std::lock_guard lock(guard); if (const auto it = runtimes.find(rt); it != runtimes.end()) {
        it->second->ready_kernel = false; it->second->ready_feed = false;
        it->second->check_next = true;
    }
}
void technique(api::effect_runtime *rt, api::effect_technique tech, bool present) {
    if (!enabled()) return;
    const int type = kind(rt, tech); if (!type) return;
    std::lock_guard lock(guard);
    if (const auto it = runtimes.find(rt); it != runtimes.end()) {
        auto &s = *it->second;
        if (s.active) { if (type == 1) ++s.kernel; else ++s.feed; }
        else if (present || s.in_present) { if (type == 1) s.ready_kernel = true; else { s.ready_feed = true; ++s.present_feed; } }
    }
}
void finish_present(api::command_list *cmd) {
    std::lock_guard lock(guard);
    for (auto &[rt, entry] : runtimes) {
        (void)rt; auto &s = *entry;
        if (s.cmd != cmd) continue;
        s.in_present = false;
        if (!s.pending) continue;
        const auto duplicate = s.present_feed - s.pending_before;
        if (duplicate) { ++s.duplicates; s.disabled = true; }
        if (s.calls <= 4 || duplicate || s.calls % 300 == 0) {
            char line[200]; std::snprintf(line, sizeof(line), "[FSObserver] early_neural_present call=%llu feed_at_later_present=%llu suppressed=%d", s.calls, duplicate, duplicate == 0); log(line);
        }
        s.pending = false;
    }
}
void run(const FSSceneReadyV1 &p, bool correlated, api::command_list *cmd) {
    if (!enabled() || !copy_probe::valid_scene(p, correlated) || p.client_extent[0] > 4096 || p.client_extent[1] > 4096) return;
    std::shared_ptr<state> s;
    {
        std::lock_guard lock(guard);
        for (const auto &[rt, candidate] : runtimes) { (void)rt; if (candidate->cmd == cmd) { if (s) return; s = candidate; } }
        if (!s || s->disabled || s->active || s->pending || !s->ready_kernel || !s->ready_feed || GetTickCount64() < s->retry_after) return;
    }
    auto *rt = s->rt;
    if (rt->get_hwnd() != static_cast<void *>(WindowFromDC(wglGetCurrentDC())) || !rt->get_effects_state()) return;
    const auto started = clock::now();
    unsigned count = 0; int order = 0; bool allowed = true;
    rt->enumerate_techniques(nullptr, [&](api::effect_runtime *runtime, api::effect_technique tech) {
        if (!runtime->get_technique_state(tech)) return;
        ++count; const int type = kind(runtime, tech); allowed = allowed && type != 0; order |= type;
    });
    if (!allowed || count != 2 || order != 3) return;
    const bool check = s->check_next || !s->scene || s->width != unsigned(p.client_extent[0]) ||
        s->height != unsigned(p.client_extent[1]) || s->composed < 3 || (s->calls + 1) % 300 == 0 || full_checks();
    const auto before = check ? gl_snapshot::capture() : gl_snapshot::snapshot{};
    if (check && !before.valid) return;
    const auto desc = rt->get_device()->get_resource_desc(rt->get_current_back_buffer());
    if ((desc.type != api::resource_type::surface && desc.type != api::resource_type::texture_2d) ||
        desc.texture.width != unsigned(p.client_extent[0]) || desc.texture.height != unsigned(p.client_extent[1]) || desc.texture.samples > 1) return;
    if (s->scene && (s->width != desc.texture.width || s->height != desc.texture.height)) {
        if (!s->extent.observe(desc.texture.width, desc.texture.height, GetTickCount64())) return;
        // These are observer-owned GL resources. Feeder captures into its own
        // synchronized bridge resources; it does not retain these target views.
        s->scene.reset(); s->backup.reset(); s->check_next = true;
        s->waiting_guides = false;
        log("[FSObserver] early_neural resize=owned_targets_released waiting_for_matching_guides");
    }
    if (!s->scene) {
        if (!s->extent.observe(desc.texture.width, desc.texture.height, GetTickCount64())) return;
        unsigned guides = 0; bool matching = true;
        rt->enumerate_texture_variables("DLSS5_Feed.fx", [&](api::effect_runtime *runtime, api::effect_texture_variable variable) {
            char name[256] = {}; runtime->get_texture_variable_name(variable, name);
            const bool motion = std::strcmp(name, "DLSS5_MV") == 0;
            if (!motion && std::strcmp(name, "DLSS5_Depth") != 0) return;
            api::resource_view srv{}, srgb{}; runtime->get_texture_binding(variable, &srv, &srgb);
            const auto resource = srv != 0 ? runtime->get_device()->get_resource_from_view(srv) : api::resource{};
            if (resource == 0) { matching = false; return; }
            const auto guide = runtime->get_device()->get_resource_desc(resource);
            const bool valid = guide.texture.width == desc.texture.width && guide.texture.height == desc.texture.height &&
                guide.texture.samples == 1 && guide.texture.format == (motion ? api::format::r16g16_float : api::format::r32_float);
            ++guides; matching = matching && valid;
            if (!s->waiting_guides) {
            char line[300]; std::snprintf(line, sizeof(line),
                "[FSObserver] early_neural_input name=%s extent=%u,%u format=%u matches_processing=%d original_depth=%u extent=%u,%u",
                name, guide.texture.width, guide.texture.height, unsigned(guide.texture.format), valid,
                p.depth_texture, p.depth_extent[0], p.depth_extent[1]); log(line);
            }
        });
        if (!matching || guides != 2) {
            if (!s->waiting_guides) log("[FSObserver] early_neural resize=waiting_for_matching_guides (temporary; automatic retry)");
            s->waiting_guides = true; s->retry_after = GetTickCount64() + 250; return;
        }
        auto scene = std::make_unique<target>(), backup = std::make_unique<target>();
        if (!scene->create(rt->get_device(), desc.texture.width, desc.texture.height) || !backup->create(rt->get_device(), desc.texture.width, desc.texture.height)) return;
        s->width = desc.texture.width; s->height = desc.texture.height; s->scene = std::move(scene); s->backup = std::move(backup);
        s->waiting_guides = false; ++s->target_generations; s->check_next = true;
        char line[220]; std::snprintf(line, sizeof(line), "[FSObserver] early_neural resize=targets_ready generation=%llu processing=%u,%u",
            s->target_generations, s->width, s->height); log(line);
    }
    if (check && !gl_snapshot::equal(before, gl_snapshot::capture())) {
        std::lock_guard lock(guard); s->disabled = true; log("[FSObserver] early_neural failed=target_setup_state"); return;
    }
    struct action_data {
        state *s; bool check, state_equal = false; uint64_t kernel = 0, feed = 0;
        clock::time_point entered{}, returned{};
    } data{s.get(), check};
    const auto bind_stage = [](void *opaque) {
        const auto &d = *static_cast<action_data *>(opaque); const api::render_pass_render_target_desc target{d.s->scene->view};
        d.s->cmd->begin_render_pass(1, &target, nullptr); d.s->cmd->end_render_pass();
    };
    const auto bind_backup = [](void *opaque) {
        const auto &d = *static_cast<action_data *>(opaque); const api::render_pass_render_target_desc target{d.s->backup->view};
        d.s->cmd->begin_render_pass(1, &target, nullptr); d.s->cmd->end_render_pass();
    };
    const auto execute = [](void *opaque) -> bool {
        auto &d = *static_cast<action_data *>(opaque);
        const auto saved = d.check ? gl_snapshot::capture() : gl_snapshot::snapshot{};
        { std::lock_guard lock(guard); d.s->active = true; ++d.s->calls; d.s->pending = true;
            d.s->pending_before = d.s->present_feed; d.s->call_kernel = d.s->kernel; d.s->call_feed = d.s->feed; }
        d.entered = clock::now();
        try { d.s->rt->render_effects(d.s->cmd, d.s->scene->view, d.s->scene->view); }
        catch (...) { std::lock_guard lock(guard); d.s->active = false; d.s->disabled = true; throw; }
        d.returned = clock::now();
        d.state_equal = !d.check || gl_snapshot::equal(saved, gl_snapshot::capture());
        std::lock_guard lock(guard); d.s->active = false;
        d.kernel = d.s->kernel - d.s->call_kernel; d.feed = d.s->feed - d.s->call_feed;
        return d.state_equal && d.kernel == 1 && d.feed == 1;
    };
    const bool composed = copy_probe::process_world(p, bind_stage, bind_backup, execute, &data);
    const bool state_equal = !check || gl_snapshot::equal(before, gl_snapshot::capture());
    const auto finished = clock::now();
    std::lock_guard lock(guard);
    if (check) ++s->checked;
    s->check_next = false;
    if (data.returned != clock::time_point{}) {
        ++s->timed; s->prep_ms += milliseconds(data.entered - started);
        s->effects_ms += milliseconds(data.returned - data.entered);
        s->compose_ms += milliseconds(finished - data.returned);
        s->max_ms = (std::max)(s->max_ms, milliseconds(finished - started));
    }
    const bool warming = !composed && data.state_equal && state_equal && data.kernel == 0 && data.feed == 0;
    if (composed && state_equal) ++s->composed;
    else if (warming) s->retry_after = GetTickCount64() + 2000;
    else s->disabled = true;
    if (s->calls <= 4 || !composed || s->calls % 300 == 0) {
        char line[500]; std::snprintf(line, sizeof(line),
            "[FSObserver] early_neural status=%s call=%llu composed=%llu world=%d,%d,%d,%d processing=%u,%u kernel=%llu feed=%llu state_equal=%d broad_state_checked=%d (existing Feeder/consumer; scene resampled, UI drawn afterward)",
            composed && state_equal ? "composed" : warming ? "permutation_pending" : "disabled_failure", s->calls, s->composed,
            p.world_rect[0], p.world_rect[1], p.world_rect[2], p.world_rect[3], s->width, s->height, data.kernel, data.feed, state_equal, check); log(line);
    }
    if (s->calls && s->calls % 300 == 0) timings(*s);
}
}
