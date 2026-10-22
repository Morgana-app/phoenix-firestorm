#include <Windows.h>
#include <GL/gl.h>
#include "copy_probe.hpp"
#include <array>
#include <algorithm>
#include <atomic>
#include <vector>
#include <cmath>
#include <cstring>
#include <limits>

namespace copy_probe {
namespace {
constexpr GLenum read_fbo = 0x8CA8, draw_fbo = 0x8CA9, read_binding = 0x8CAA, draw_binding = 0x8CA6;
constexpr GLenum attachment = 0x8CE0, complete = 0x8CD5, srgb = 0x8DB9;
constexpr GLenum pack_buffer = 0x88EB, unpack_buffer = 0x88EC, pack_binding = 0x88ED, unpack_binding = 0x88EF;
using bind_fbo_fn = void (APIENTRY *)(GLenum, GLuint);
using gen_fn = void (APIENTRY *)(GLsizei, GLuint *);
using delete_fn = void (APIENTRY *)(GLsizei, const GLuint *);
using attach_fn = void (APIENTRY *)(GLenum, GLenum, GLenum, GLuint, GLint);
using check_fn = GLenum (APIENTRY *)(GLenum);
using blit_fn = void (APIENTRY *)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
using bind_buffer_fn = void (APIENTRY *)(GLenum, GLuint);
using attachment_query_fn = void (APIENTRY *)(GLenum, GLenum, GLenum, GLint *);
template<class T> T load(const char *name) {
    const auto proc = wglGetProcAddress(name);
    const auto value = reinterpret_cast<uintptr_t>(proc);
    return value > 3 && value != UINTPTR_MAX ? reinterpret_cast<T>(proc) : nullptr;
}
struct functions {
    bind_fbo_fn bind = load<bind_fbo_fn>("glBindFramebuffer");
    gen_fn gen = load<gen_fn>("glGenFramebuffers");
    delete_fn del = load<delete_fn>("glDeleteFramebuffers");
    attach_fn attach = load<attach_fn>("glFramebufferTexture2D");
    check_fn check = load<check_fn>("glCheckFramebufferStatus");
    blit_fn blit = load<blit_fn>("glBlitFramebuffer");
    bind_buffer_fn buffer = load<bind_buffer_fn>("glBindBuffer");
    attachment_query_fn parameter = load<attachment_query_fn>("glGetFramebufferAttachmentParameteriv");
    bool valid() const { return bind && gen && del && attach && check && blit && buffer && parameter; }
};
constexpr std::array<GLenum, 12> queries = {read_binding, draw_binding, GL_READ_BUFFER, GL_DRAW_BUFFER,
    GL_TEXTURE_BINDING_2D, pack_binding, unpack_binding, GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH,
    GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS, 0x84E0 /* active texture */};
struct state {
    std::array<GLint, queries.size()> values = {};
    GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST), encode = glIsEnabled(srgb), dither = glIsEnabled(GL_DITHER);
    state() { for (size_t i = 0; i < queries.size(); ++i) glGetIntegerv(queries[i], &values[i]); }
    void restore(const functions &f) const {
        f.bind(read_fbo, static_cast<GLuint>(values[0])); glReadBuffer(static_cast<GLenum>(values[2]));
        f.bind(draw_fbo, static_cast<GLuint>(values[1])); glDrawBuffer(static_cast<GLenum>(values[3]));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(values[4]));
        f.buffer(pack_buffer, static_cast<GLuint>(values[5])); f.buffer(unpack_buffer, static_cast<GLuint>(values[6]));
        glPixelStorei(GL_PACK_ALIGNMENT, values[7]); glPixelStorei(GL_PACK_ROW_LENGTH, values[8]);
        glPixelStorei(GL_PACK_SKIP_PIXELS, values[9]); glPixelStorei(GL_PACK_SKIP_ROWS, values[10]);
        const auto enable = [](GLenum cap, GLboolean on) { if (on) glEnable(cap); else glDisable(cap); };
        enable(GL_SCISSOR_TEST, scissor); enable(srgb, encode); enable(GL_DITHER, dither);
    }
    bool operator==(const state &other) const {
        return values == other.values && scissor == other.scissor && encode == other.encode && dither == other.dither;
    }
};
struct transaction {
    const functions &f;
    state saved;
    GLuint texture = 0, framebuffer = 0;
    GLuint output_texture = 0, output_framebuffer = 0;
    ~transaction() {
        saved.restore(f);
        if (framebuffer) f.del(1, &framebuffer);
        if (texture) glDeleteTextures(1, &texture);
        if (output_framebuffer) f.del(1, &output_framebuffer);
        if (output_texture) glDeleteTextures(1, &output_texture);
    }
};
bool flip_default(const functions &f, transaction &t, int width, int height, GLint format)
{
    // ReShade's API flips default-framebuffer -> texture copies. Its ordinary
    // present path has a matching texture -> default copy. A direct early call
    // needs that second orientation conversion explicitly, outside the shader.
    f.buffer(unpack_buffer, 0);
    glDisable(GL_SCISSOR_TEST); glDisable(srgb); glDisable(GL_DITHER);
    glGenTextures(1, &t.output_texture); glBindTexture(GL_TEXTURE_2D, t.output_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    f.gen(1, &t.output_framebuffer); f.bind(draw_fbo, t.output_framebuffer);
    f.attach(draw_fbo, attachment, GL_TEXTURE_2D, t.output_texture, 0); glDrawBuffer(attachment);
    if (f.check(draw_fbo) != complete) return false;
    f.bind(read_fbo, 0); glReadBuffer(GL_BACK);
    f.blit(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    f.bind(read_fbo, t.output_framebuffer); glReadBuffer(attachment);
    f.bind(draw_fbo, 0); glDrawBuffer(GL_BACK);
    f.blit(0, 0, width, height, 0, height, width, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    f.bind(read_fbo, 0); glReadBuffer(GL_BACK);
    return true;
}
std::atomic<unsigned> attempt_counts[2] = {};
std::atomic<bool> failures[2] = {};
std::atomic<unsigned> world_attempts{0};
std::atomic<bool> world_failed{false};
}
#ifdef FS_PROBE_TEST_HOOKS
bool test_flip_default(int width, int height)
{
    functions f; if (!f.valid()) return false;
    transaction t{f};
    return flip_default(f, t, width, height, GL_RGBA8);
}
#endif
void measure_color(const std::vector<unsigned char> &before, const std::vector<unsigned char> &after, result &r)
{
    for (int y = 0; y < r.height; ++y) for (int x = 0; x < r.width; ++x) {
        const auto at = (size_t(y) * size_t(r.width) + size_t(x)) * 4;
        const auto flip = (size_t(r.height - 1 - y) * size_t(r.width) + size_t(x)) * 4;
        bool changed = false, flipped_changed = false;
        for (size_t channel = 0; channel < 3; ++channel) {
            const int delta = int(before[at + channel]) - int(after[at + channel]);
            const auto magnitude = unsigned(delta < 0 ? -delta : delta);
            if (magnitude > r.max_rgb_delta) r.max_rgb_delta = magnitude;
            changed = changed || magnitude != 0;
            flipped_changed = flipped_changed || before[flip + channel] != after[at + channel];
        }
        r.changed_rgb_pixels += changed;
        r.vertical_flip_changed_pixels += flipped_changed;
        r.changed_alpha_pixels += before[at + 3] != after[at + 3];
    }
}
result run(const FSSceneReadyV1 &p, bool correlated, void (*action)(void *), void *data, bool correct_default_orientation)
{
    result r;
    char enabled[8] = {};
    if (GetEnvironmentVariableA(action ? "FS_SCENE_IDENTITY_PROBE" : "FS_SCENE_COPY_PROBE", enabled, sizeof(enabled)) != 1 || enabled[0] != '1') return r;
    auto &attempts = attempt_counts[action ? 1 : 0];
    auto &failed = failures[action ? 1 : 0];
    if (failed) { r.status = "disabled_after_failure"; return r; }
    if (!full_scene(p, correlated)) { r.status = "skip_scene_or_coverage"; return r; }
    if (reinterpret_cast<uintptr_t>(wglGetCurrentContext()) != p.context_cookie) { r.status = "skip_context"; return r; }
    r.width = p.client_extent[0]; r.height = p.client_extent[1];
    if (r.width > 4096 || r.height > 4096) { r.status = "skip_size_limit"; return r; }
    RECT client = {};
    if (!GetClientRect(WindowFromDC(wglGetCurrentDC()), &client) || client.right != r.width || client.bottom != r.height) {
        r.status = "skip_live_extent"; return r;
    }
    functions f;
    if (!f.valid()) { r.status = "skip_functions"; return r; }
    state initial;
    if (initial.values[0] || initial.values[1] || initial.values[2] != GL_BACK || initial.values[3] != GL_BACK) {
        r.status = "skip_target"; return r;
    }
    GLint red = 0, green = 0, blue = 0, alpha = 0, samples = 0, depth = 0, stencil = 0;
    // GL_RED_BITS/GL_DEPTH_BITS etc. are removed in a core context. Query the
    // actual default-framebuffer attachments through the supported core API.
    f.parameter(read_fbo, GL_BACK_LEFT, 0x8212 /* red size */, &red);
    f.parameter(read_fbo, GL_BACK_LEFT, 0x8213 /* green size */, &green);
    f.parameter(read_fbo, GL_BACK_LEFT, 0x8214 /* blue size */, &blue);
    f.parameter(read_fbo, GL_BACK_LEFT, 0x8215 /* alpha size */, &alpha);
    glGetIntegerv(0x80A9 /* samples */, &samples);
    GLint depth_type = 0, stencil_type = 0;
    f.parameter(read_fbo, GL_DEPTH, 0x8CD0 /* object type */, &depth_type);
    f.parameter(read_fbo, GL_STENCIL, 0x8CD0 /* object type */, &stencil_type);
    if (depth_type != GL_NONE) f.parameter(read_fbo, GL_DEPTH, 0x8216 /* depth size */, &depth);
    if (stencil_type != GL_NONE) f.parameter(read_fbo, GL_STENCIL, 0x8217 /* stencil size */, &stencil);
    r.red = red; r.green = green; r.blue = blue; r.alpha = alpha;
    r.samples = samples; r.depth_bits = depth; r.stencil_bits = stencil;
    if (samples || red != 8 || green != 8 || blue != 8 || (alpha != 0 && alpha != 8) || !depth) {
        r.status = "skip_format_or_samples"; return r;
    }
    if (attempts.fetch_add(1) >= 3) { r.status = "budget_complete"; return r; }
    const size_t pixels = size_t(r.width) * size_t(r.height);
    // Allocate all CPU storage before changing GL state. Readbacks are deliberately
    // synchronous diagnostics, not a production/performance path.
    std::vector<unsigned char> before(pixels * 4), staged(pixels * 4), after(pixels * 4);
    std::vector<float> depth_before(pixels), depth_after(pixels);
    std::vector<unsigned char> stencil_before(stencil ? pixels : 0), stencil_after(stencil ? pixels : 0);
    r.attempted = true;
    {
        transaction t{f};
        f.buffer(pack_buffer, 0); f.buffer(unpack_buffer, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0); glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glDisable(GL_SCISSOR_TEST); glDisable(srgb); glDisable(GL_DITHER);
        glReadPixels(0, 0, r.width, r.height, GL_RGBA, GL_UNSIGNED_BYTE, before.data());
        glReadPixels(0, 0, r.width, r.height, GL_DEPTH_COMPONENT, GL_FLOAT, depth_before.data());
        if (stencil) glReadPixels(0, 0, r.width, r.height, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencil_before.data());
        // Keep a verified GPU copy of the original image for a failed P1 call.
        glGenTextures(1, &t.texture); glBindTexture(GL_TEXTURE_2D, t.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, alpha ? GL_RGBA8 : GL_RGB8, r.width, r.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        f.gen(1, &t.framebuffer); f.bind(draw_fbo, t.framebuffer);
        f.attach(draw_fbo, attachment, GL_TEXTURE_2D, t.texture, 0); glDrawBuffer(attachment);
        if (f.check(draw_fbo) != complete) { failed = true; r.status = "failed_staging_target"; return r; }
        f.blit(0, 0, r.width, r.height, 0, 0, r.width, r.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        f.bind(read_fbo, t.framebuffer); glReadBuffer(attachment);
        glReadPixels(0, 0, r.width, r.height, GL_RGBA, GL_UNSIGNED_BYTE, staged.data());
        r.stage_equal = before == staged;
        // Fail closed before modifying presentation if the capture is not exact.
        if (!r.stage_equal) { failed = true; r.status = "failed_capture_compare"; return r; }
        if (action) {
            t.saved.restore(f);
            action(data);
            r.state_equal = initial == state{};
            f.buffer(pack_buffer, 0); f.buffer(unpack_buffer, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
            glPixelStorei(GL_PACK_SKIP_PIXELS, 0); glPixelStorei(GL_PACK_SKIP_ROWS, 0);
            f.bind(read_fbo, 0); glReadBuffer(GL_BACK);
            if (correct_default_orientation) {
                glReadPixels(0, 0, r.width, r.height, GL_RGBA, GL_UNSIGNED_BYTE, after.data());
                result raw; raw.width = r.width; raw.height = r.height;
                measure_color(before, after, raw);
                r.raw_changed_rgb_pixels = raw.changed_rgb_pixels;
                r.raw_vertical_flip_changed_pixels = raw.vertical_flip_changed_pixels;
                r.orientation_corrected = flip_default(f, t, r.width, r.height, alpha ? GL_RGBA8 : GL_RGB8);
            }
        } else {
        f.bind(draw_fbo, 0); glDrawBuffer(GL_BACK);
        f.blit(0, 0, r.width, r.height, 0, 0, r.width, r.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        f.bind(read_fbo, 0); glReadBuffer(GL_BACK);
        }
        glReadPixels(0, 0, r.width, r.height, GL_RGBA, GL_UNSIGNED_BYTE, after.data());
        glReadPixels(0, 0, r.width, r.height, GL_DEPTH_COMPONENT, GL_FLOAT, depth_after.data());
        if (stencil) glReadPixels(0, 0, r.width, r.height, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencil_after.data());
        r.color_equal = before == after;
        r.depth_equal = depth_before == depth_after;
        r.stencil_equal = stencil_before == stencil_after;
        measure_color(before, after, r);
        if (action && !r.color_equal) {
            // Preserve the original current scene before returning to the viewer.
            // The failed comparison stays failed; rollback is a separate witness.
            glDisable(GL_SCISSOR_TEST); glDisable(srgb); glDisable(GL_DITHER);
            f.bind(read_fbo, t.framebuffer); glReadBuffer(attachment);
            f.bind(draw_fbo, 0); glDrawBuffer(GL_BACK);
            f.blit(0, 0, r.width, r.height, 0, 0, r.width, r.height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            f.bind(read_fbo, 0); glReadBuffer(GL_BACK);
            glReadPixels(0, 0, r.width, r.height, GL_RGBA, GL_UNSIGNED_BYTE, after.data());
            r.rollback_equal = before == after;
        }
    }
    r.state_equal = (!action || r.state_equal) && initial == state{};
    const bool orientation_ok = !correct_default_orientation || r.orientation_corrected;
    r.status = r.color_equal && r.depth_equal && r.stencil_equal && r.state_equal && orientation_ok ? "passed" : "failed_roundtrip_or_state";
    if (!r.color_equal || !r.depth_equal || !r.stencil_equal || !r.state_equal || !orientation_ok) failed = true;
    return r;
}

result run_world(const FSSceneReadyV1 &p, bool correlated, void (*bind_stage)(void *), void (*execute)(void *), void *data)
{
    result r;
    char option[8] = {};
    if (GetEnvironmentVariableA("FS_SCENE_WORLD_PROBE", option, sizeof(option)) != 1 || option[0] != '1') return r;
    if (!valid_scene(p, correlated) || !bind_stage || !execute) { r.status = "skip_world_scene"; return r; }
    if (world_failed) { r.status = "world_disabled_after_failure"; return r; }
    if (reinterpret_cast<uintptr_t>(wglGetCurrentContext()) != p.context_cookie) { r.status = "skip_context"; return r; }
    const int cw = p.client_extent[0], ch = p.client_extent[1];
    const int x = p.world_rect[0], y = p.world_rect[1], w = p.world_rect[2], h = p.world_rect[3];
    r.width = w; r.height = h;
    if (cw > 4096 || ch > 4096) { r.status = "skip_size_limit"; return r; }
    RECT client{};
    if (!GetClientRect(WindowFromDC(wglGetCurrentDC()), &client) || client.right != cw || client.bottom != ch) {
        r.status = "skip_live_extent"; return r;
    }
    functions f;
    if (!f.valid()) { r.status = "skip_functions"; return r; }
    const state initial;
    if (initial.values[0] || initial.values[1] || initial.values[2] != GL_BACK || initial.values[3] != GL_BACK) {
        r.status = "skip_target"; return r;
    }
    f.parameter(read_fbo, GL_BACK_LEFT, 0x8212, &r.red); f.parameter(read_fbo, GL_BACK_LEFT, 0x8213, &r.green);
    f.parameter(read_fbo, GL_BACK_LEFT, 0x8214, &r.blue); f.parameter(read_fbo, GL_BACK_LEFT, 0x8215, &r.alpha);
    glGetIntegerv(0x80A9, &r.samples);
    GLint depth_type = 0, stencil_type = 0;
    f.parameter(read_fbo, GL_DEPTH, 0x8CD0, &depth_type); f.parameter(read_fbo, GL_STENCIL, 0x8CD0, &stencil_type);
    if (depth_type) f.parameter(read_fbo, GL_DEPTH, 0x8216, &r.depth_bits);
    if (stencil_type) f.parameter(read_fbo, GL_STENCIL, 0x8217, &r.stencil_bits);
    if (r.red != 8 || r.green != 8 || r.blue != 8 || (r.alpha != 0 && r.alpha != 8) || r.samples || !r.depth_bits) {
        r.status = "skip_format_or_samples"; return r;
    }
    if (world_attempts.fetch_add(1) >= 12) { r.status = "world_attempt_budget_complete"; return r; }
    const size_t pixels = size_t(cw) * size_t(ch), world_pixels = size_t(w) * size_t(h);
    std::vector<unsigned char> before(pixels * 4), after(pixels * 4), backup(pixels * 4);
    std::vector<unsigned char> crop(world_pixels * 4), stage(world_pixels * 4);
    std::vector<float> depth_before(pixels), depth_after(pixels);
    std::vector<unsigned char> stencil_before(r.stencil_bits ? pixels : 0), stencil_after(r.stencil_bits ? pixels : 0);
    r.attempted = true;
    {
        transaction t{f};
        const auto pack = [&] {
            f.buffer(pack_buffer, 0); f.buffer(unpack_buffer, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
            glPixelStorei(GL_PACK_SKIP_PIXELS, 0); glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        };
        const auto plain_blit = [&] { glDisable(GL_SCISSOR_TEST); glDisable(srgb); glDisable(GL_DITHER); };
        pack();
        glReadPixels(0, 0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, before.data());
        glReadPixels(0, 0, cw, ch, GL_DEPTH_COMPONENT, GL_FLOAT, depth_before.data());
        if (r.stencil_bits) glReadPixels(0, 0, cw, ch, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencil_before.data());
        for (int row = 0; row < h; ++row)
            std::copy_n(before.data() + (size_t(y + row) * size_t(cw) + size_t(x)) * 4,
                size_t(w) * 4, crop.data() + size_t(row) * size_t(w) * 4);
        // Full-window backup permits fallback even if an unexpected API operation
        // modifies pixels outside the world rectangle. Verify it before effects.
        plain_blit();
        glGenTextures(1, &t.texture); glBindTexture(GL_TEXTURE_2D, t.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, r.alpha ? GL_RGBA8 : GL_RGB8, cw, ch, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        f.gen(1, &t.framebuffer); f.bind(draw_fbo, t.framebuffer);
        f.attach(draw_fbo, attachment, GL_TEXTURE_2D, t.texture, 0); glDrawBuffer(attachment);
        if (f.check(draw_fbo) != complete) { world_failed = true; r.status = "failed_world_backup_target"; return r; }
        f.blit(0, 0, cw, ch, 0, 0, cw, ch, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        f.bind(read_fbo, t.framebuffer); glReadBuffer(attachment);
        glReadPixels(0, 0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, backup.data());
        if (before != backup) { world_failed = true; r.status = "failed_world_backup_compare"; return r; }
        // A fresh FBO identity is obtained after each API operation, since the
        // backend may invalidate/rebuild its own FBO cache during permutation load.
        const auto stage_fbo = [&]() -> GLuint {
            t.saved.restore(f);
            bind_stage(data);
            GLint framebuffer = 0; glGetIntegerv(draw_binding, &framebuffer);
            const bool ready = framebuffer != 0 && f.check(draw_fbo) == complete;
            t.saved.restore(f);
            return ready ? static_cast<GLuint>(framebuffer) : 0;
        };
        GLuint target = stage_fbo();
        if (!target) { world_failed = true; r.status = "failed_world_stage_target"; return r; }
        plain_blit(); pack();
        f.bind(read_fbo, 0); glReadBuffer(GL_BACK);
        f.bind(draw_fbo, target); glDrawBuffer(attachment);
        f.blit(x, y, x + w, y + h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        f.bind(read_fbo, target); glReadBuffer(attachment);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, stage.data());
        r.stage_equal = stage == crop;
        if (!r.stage_equal) { world_failed = true; r.status = "failed_world_capture_compare"; return r; }
        t.saved.restore(f);
        execute(data);
        r.state_equal = initial == state{};
        target = stage_fbo();
        pack();
        if (target) {
            f.bind(read_fbo, target); glReadBuffer(attachment);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, stage.data());
            r.stage_output_equal = stage == crop;
            measure_color(crop, stage, r);
            if (r.stage_output_equal) {
                plain_blit(); f.bind(draw_fbo, 0); glDrawBuffer(GL_BACK);
                // Both transfers are native GL copies in one lower-left coordinate
                // convention. No default-target API flip or scale is involved.
                f.blit(0, 0, w, h, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                r.composited = true;
            }
        }
        f.bind(read_fbo, 0); glReadBuffer(GL_BACK);
        glReadPixels(0, 0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, after.data());
        glReadPixels(0, 0, cw, ch, GL_DEPTH_COMPONENT, GL_FLOAT, depth_after.data());
        if (r.stencil_bits) glReadPixels(0, 0, cw, ch, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, stencil_after.data());
        r.color_equal = before == after; r.depth_equal = depth_before == depth_after; r.stencil_equal = stencil_before == stencil_after;
        r.outside_equal = true;
        for (int row = 0; row < ch; ++row) for (int column = 0; column < cw; ++column) {
            if (column >= x && column < x + w && row >= y && row < y + h) continue;
            const size_t at = (size_t(row) * size_t(cw) + size_t(column)) * 4;
            for (size_t channel = 0; channel < 4; ++channel)
                r.outside_equal = r.outside_equal && before[at + channel] == after[at + channel];
        }
        if (!r.color_equal) {
            plain_blit(); f.bind(read_fbo, t.framebuffer); glReadBuffer(attachment);
            f.bind(draw_fbo, 0); glDrawBuffer(GL_BACK);
            f.blit(0, 0, cw, ch, 0, 0, cw, ch, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            f.bind(read_fbo, 0); glReadBuffer(GL_BACK);
            glReadPixels(0, 0, cw, ch, GL_RGBA, GL_UNSIGNED_BYTE, after.data());
            r.rollback_equal = before == after;
        }
    }
    r.state_equal = r.state_equal && initial == state{};
    const bool pass = r.stage_equal && r.stage_output_equal && r.composited && r.color_equal && r.outside_equal &&
        r.depth_equal && r.stencil_equal && r.state_equal;
    if (!pass) world_failed = true;
    r.status = pass ? "passed" : "failed_world_image_or_state";
    return r;
}

bool process_world(const FSSceneReadyV1 &p, void (*bind_stage)(void *), void (*bind_backup)(void *),
    bool (*execute)(void *), void *data)
{
    if (!valid_scene(p, true) || reinterpret_cast<uintptr_t>(wglGetCurrentContext()) != p.context_cookie ||
        !bind_stage || !bind_backup || !execute) return false;
    functions f; if (!f.valid()) return false;
    transaction t{f};
    if (t.saved.values[0] || t.saved.values[1] || t.saved.values[2] != GL_BACK || t.saved.values[3] != GL_BACK) return false;
    const int cw = p.client_extent[0], ch = p.client_extent[1];
    RECT client{};
    if (!GetClientRect(WindowFromDC(wglGetCurrentDC()), &client) || client.right != cw || client.bottom != ch) return false;
    const int x = p.world_rect[0], y = p.world_rect[1], w = p.world_rect[2], h = p.world_rect[3];
    const auto framebuffer = [&](void (*bind)(void *)) -> GLuint {
        t.saved.restore(f); bind(data);
        GLint value = 0; glGetIntegerv(draw_binding, &value);
        const bool valid = value != 0 && f.check(draw_fbo) == complete;
        t.saved.restore(f); return valid ? static_cast<GLuint>(value) : 0;
    };
    const auto plain = [&] { glDisable(GL_SCISSOR_TEST); glDisable(srgb); glDisable(GL_DITHER); };
    GLuint backup = framebuffer(bind_backup), stage = framebuffer(bind_stage);
    if (!backup || !stage || backup == stage) return false;
    plain();
    f.bind(read_fbo, 0); glReadBuffer(GL_BACK); f.bind(draw_fbo, backup); glDrawBuffer(attachment);
    f.blit(0, 0, cw, ch, 0, 0, cw, ch, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    f.bind(draw_fbo, stage); glDrawBuffer(attachment);
    f.blit(x, y, x + w, y + h, 0, 0, cw, ch, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    t.saved.restore(f);
    bool completed = false;
    try { completed = execute(data); }
    catch (...) { completed = false; }
    // Re-query both API FBOs after the call; effect creation can invalidate caches.
    backup = framebuffer(bind_backup); stage = framebuffer(bind_stage);
    if (!backup) return false;
    plain(); f.bind(read_fbo, backup); glReadBuffer(attachment);
    f.bind(draw_fbo, 0); glDrawBuffer(GL_BACK);
    f.blit(0, 0, cw, ch, 0, 0, cw, ch, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    if (completed && stage) {
        f.bind(read_fbo, stage); glReadBuffer(attachment);
        f.blit(0, 0, cw, ch, x, y, x + w, y + h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
    }
    t.saved.restore(f);
    return completed && stage && t.saved == state{};
}

depth_readback read_depth(uint32_t texture, int expected_width, int expected_height)
{
    depth_readback r;
    using parameter_fn = void (APIENTRY *)(GLuint, GLint, GLenum, GLint *);
    using read_fn = void (APIENTRY *)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, GLsizei, void *);
    const auto parameter = load<parameter_fn>("glGetTextureLevelParameteriv");
    const auto read = load<read_fn>("glGetTextureSubImage");
    const auto buffer = load<bind_buffer_fn>("glBindBuffer");
    if (!parameter || !read || !buffer || !texture || !glIsTexture(texture)) return r;
    parameter(texture, 0, GL_TEXTURE_WIDTH, &r.width); parameter(texture, 0, GL_TEXTURE_HEIGHT, &r.height);
    parameter(texture, 0, GL_TEXTURE_INTERNAL_FORMAT, &r.format);
    if (r.width != expected_width || r.height != expected_height || r.width < 32 || r.height < 32) {
        r.status = "skip_depth_extent"; return r;
    }
    if (r.format != 0x81A5 && r.format != 0x81A6 && r.format != 0x81A7 &&
        r.format != 0x8CAC && r.format != 0x88F0 && r.format != 0x8CAD) {
        r.status = "skip_non_depth_format"; return r;
    }
    const state before;
    constexpr GLenum stores[] = {GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS,
        0x806C /* image height */, 0x806B /* skip images */, GL_PACK_SWAP_BYTES, GL_PACK_LSB_FIRST};
    GLint saved[8] = {};
    for (unsigned i = 0; i < 8; ++i) { glGetIntegerv(stores[i], &saved[i]); glPixelStorei(stores[i], i == 0 ? 1 : 0); }
    buffer(pack_buffer, 0);
    const int xs[] = {0, (r.width - 32) / 2, r.width - 32};
    const int ys[] = {0, (r.height - 32) / 2, r.height - 32};
    r.values.fill(std::numeric_limits<float>::quiet_NaN());
    for (unsigned tile = 0; tile < 3; ++tile)
        read(texture, 0, xs[tile], ys[tile], 0, 32, 32, 1, GL_DEPTH_COMPONENT, GL_FLOAT,
            32 * 32 * sizeof(float), r.values.data() + tile * 32 * 32);
    buffer(pack_buffer, static_cast<GLuint>(before.values[5]));
    for (unsigned i = 0; i < 8; ++i) glPixelStorei(stores[i], saved[i]);
    r.state_equal = before == state{};
    r.minimum = 1; r.maximum = 0; r.hash = 14695981039346656037ull;
    for (const auto value : r.values) {
        ++r.count;
        if (std::isfinite(value)) { ++r.finite; r.minimum = (std::min)(r.minimum, value); r.maximum = (std::max)(r.maximum, value); }
        uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); r.hash = (r.hash ^ bits) * 1099511628211ull;
    }
    r.valid = r.state_equal && r.finite == r.count && r.minimum >= 0 && r.maximum <= 1;
    r.status = r.valid ? "sampled" : "failed_depth_sample_or_state";
    return r;
}
}
