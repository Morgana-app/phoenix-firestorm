#include <Windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include "copy_probe.hpp"
#include "gl_snapshot.hpp"
#include <cstdio>

int main(int argc, char **)
{
    {
        const std::vector<unsigned char> original = {10,20,30,255, 40,50,60,255};
        const std::vector<unsigned char> flipped = {40,50,60,255, 10,20,30,255};
        copy_probe::result difference; difference.width = 1; difference.height = 2;
        copy_probe::measure_color(original, flipped, difference);
        if (difference.changed_rgb_pixels != 2 || difference.vertical_flip_changed_pixels != 0 ||
            difference.max_rgb_delta != 30 || difference.changed_alpha_pixels != 0) return 1;
    }
    FSSceneReadyV1 p{};
    p.struct_size = sizeof(p); p.abi_version = FS_SCENE_ABI_VERSION;
    p.display_serial = p.scene_serial = p.context_cookie = 1;
    p.purpose = FS_SCENE_MAIN;
    p.conditions = FS_SCENE_STARTED | FS_SCENE_CONNECTED | FS_SCENE_WORLD_CAMERA | FS_SCENE_PRESENT_INTENDED;
    p.color_texture = p.depth_texture = 1; p.color_target = p.depth_target = GL_TEXTURE_2D;
    p.world_rect[2] = p.viewport[2] = p.client_extent[0] = 64;
    p.world_rect[3] = p.viewport[3] = p.client_extent[1] = 48;
    p.color_extent[0] = p.depth_extent[0] = 64; p.color_extent[1] = p.depth_extent[1] = 48;
    const auto require = [](bool ok, const char *what) { if (!ok) { std::fprintf(stderr, "%s\n", what); return false; } return true; };
    if (!require(copy_probe::full_scene(p, true), "valid full scene rejected") ||
        !require(!copy_probe::full_scene(p, false), "uncorrelated scene accepted")) return 1;
    p.client_extent[1] = 71;
    if (!require(!copy_probe::full_scene(p, true), "partial scene accepted")) return 1;
    p.client_extent[1] = 48; p.purpose = FS_SCENE_SNAPSHOT;
    if (!require(!copy_probe::full_scene(p, true), "snapshot accepted")) return 1;
    p.purpose = FS_SCENE_MAIN;
    SetEnvironmentVariableA("FS_SCENE_COPY_PROBE", nullptr);
    if (!require(!copy_probe::run(p, true).attempted, "default-off probe attempted GL")) return 1;

    // Hidden native test window: no activation and no viewer/user-app interaction.
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"FSCopyProbeTest";
    if (!RegisterClassW(&wc)) return 77;
    HWND window = CreateWindowW(wc.lpszClassName, L"", WS_POPUP, 0, 0, 64, 48, nullptr, nullptr, wc.hInstance, nullptr);
    if (!window) return 77;
    HDC dc = GetDC(window);
    PIXELFORMATDESCRIPTOR pd{}; pd.nSize = sizeof(pd); pd.nVersion = 1;
    pd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pd.iPixelType = PFD_TYPE_RGBA; pd.cColorBits = 24; pd.cAlphaBits = 8; pd.cDepthBits = 24; pd.cStencilBits = 8;
    const int format = ChoosePixelFormat(dc, &pd);
    if (!format || !SetPixelFormat(dc, format, &pd)) return 77;
    HGLRC context = wglCreateContext(dc);
    if (!context || !wglMakeCurrent(dc, context)) return 77;
    if (argc == 1) {
    using create_context_fn = HGLRC (WINAPI *)(HDC, HGLRC, const int *);
    const auto create_core = reinterpret_cast<create_context_fn>(wglGetProcAddress("wglCreateContextAttribsARB"));
    if (!create_core) return 77;
    const int core_attributes[] = {0x2091 /* major */, 4, 0x2092 /* minor */, 5,
        0x9126 /* profile mask */, 1 /* core profile */, 0};
    const auto core = create_core(dc, nullptr, core_attributes);
    if (!core || !wglMakeCurrent(dc, core)) return 77;
    wglDeleteContext(context); context = core;
    GLint profile = 0; glGetIntegerv(0x9126 /* context profile mask */, &profile);
    if (!require(profile == 1, "test did not obtain a core context")) return 1;
    }
    std::printf("GL renderer: %s; version: %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
    if (!wglGetProcAddress("glBlitFramebuffer")) return 77;
    p.context_cookie = reinterpret_cast<uintptr_t>(context);
    glViewport(0, 0, 64, 48); glDrawBuffer(GL_BACK); glReadBuffer(GL_BACK);
    glClearColor(0.2f, 0.4f, 0.7f, 1.0f); glClearDepth(0.25); glClearStencil(3);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST); glScissor(32, 0, 32, 48);
    glClearColor(0.8f, 0.1f, 0.3f, 0.5f); glClearDepth(0.75); glClearStencil(7);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glScissor(0, 24, 64, 24); glClearColor(0.12f, 0.58f, 0.91f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glScissor(32, 0, 32, 48);
    unsigned char left[4] = {}, right[4] = {}, stencil_left = 0, stencil_right = 0;
    float depth_left = 0, depth_right = 0;
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, left);
    glReadPixels(40, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, right);
    glReadPixels(0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth_left);
    glReadPixels(40, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth_right);
    glReadPixels(0, 0, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, &stencil_left);
    glReadPixels(40, 0, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, &stencil_right);
    if (!require(left[0] != right[0] && depth_left < depth_right && stencil_left == 3 && stencil_right == 7,
        "test framebuffer did not contain the expected nonuniform fixture")) return 1;
    glPixelStorei(GL_PACK_ALIGNMENT, 8); glPixelStorei(GL_PACK_ROW_LENGTH, 77);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 3); glPixelStorei(GL_PACK_SKIP_ROWS, 2);
    glEnable(0x8DB9 /* framebuffer sRGB */);
    using gen_buffers_fn = void (APIENTRY *)(GLsizei, GLuint *);
    using bind_buffer_fn = void (APIENTRY *)(GLenum, GLuint);
    using delete_buffers_fn = void (APIENTRY *)(GLsizei, const GLuint *);
    const auto gen_buffers = reinterpret_cast<gen_buffers_fn>(wglGetProcAddress("glGenBuffers"));
    const auto bind_buffer = reinterpret_cast<bind_buffer_fn>(wglGetProcAddress("glBindBuffer"));
    const auto delete_buffers = reinterpret_cast<delete_buffers_fn>(wglGetProcAddress("glDeleteBuffers"));
    if (!gen_buffers || !bind_buffer || !delete_buffers) return 77;
    GLuint buffers[2] = {}, original_texture = 0;
    gen_buffers(2, buffers); bind_buffer(0x88EB /* pixel pack */, buffers[0]); bind_buffer(0x88EC /* pixel unpack */, buffers[1]);
    glGenTextures(1, &original_texture); glBindTexture(GL_TEXTURE_2D, original_texture);
    SetEnvironmentVariableA("FS_SCENE_COPY_PROBE", "1");
    bool passed = true;
    for (unsigned i = 0; i < 3; ++i) {
        const auto r = copy_probe::run(p, true);
        std::printf("probe %u: %s capture=%d color=%d depth=%d stencil=%d state=%d\n", i, r.status,
            r.stage_equal, r.color_equal, r.depth_equal, r.stencil_equal, r.state_equal);
        passed = require(r.attempted && r.stage_equal && r.color_equal && r.depth_equal && r.stencil_equal && r.state_equal,
            "native GL roundtrip failed") && passed;
    }
    passed = require(!copy_probe::run(p, true).attempted, "probe exceeded three-attempt budget") && passed;
    if (argc == 1) {
        const auto baseline = gl_snapshot::capture();
        passed = require(gl_snapshot::equal(baseline, gl_snapshot::capture()), "read-only GL state snapshots differ") && passed;
        glViewport(1, 2, 32, 24);
        passed = require(!gl_snapshot::equal(baseline, gl_snapshot::capture()), "GL snapshot missed viewport change") && passed;
        glViewport(0, 0, 64, 48);
        passed = require(gl_snapshot::equal(baseline, gl_snapshot::capture()), "GL snapshot differs after manual restoration") && passed;
    }
    SetEnvironmentVariableA("FS_SCENE_IDENTITY_PROBE", "1");
    unsigned calls = 0;
    const auto noop = [](void *data) { ++*static_cast<unsigned *>(data); };
    const auto identity = copy_probe::run(p, true, noop, &calls);
    passed = require(calls == 1 && identity.attempted && identity.color_equal && identity.depth_equal &&
        identity.stencil_equal && identity.state_equal, "P1 callback verification failed") && passed;
    bool flipped = false;
    const auto simulate_default_backend = [](void *data) { *static_cast<bool *>(data) = copy_probe::test_flip_default(64, 48); };
    const auto corrected = copy_probe::run(p, true, simulate_default_backend, &flipped, true);
    passed = require(flipped && corrected.orientation_corrected && corrected.color_equal && corrected.depth_equal &&
        corrected.stencil_equal && corrected.state_equal && corrected.raw_changed_rgb_pixels > 0 &&
        corrected.raw_vertical_flip_changed_pixels == 0, "P1 default-framebuffer orientation correction failed") && passed;
    const auto mutate_pack = [](void *) {
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glClearColor(1, 1, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
    };
    const auto changed = copy_probe::run(p, true, mutate_pack);
    passed = require(changed.attempted && !changed.state_equal && !changed.color_equal &&
        changed.changed_rgb_pixels > 0 && changed.rollback_equal, "P1 missed color/state mutation or failed image rollback") && passed;
    GLint restored_pack = 0; glGetIntegerv(GL_PACK_ALIGNMENT, &restored_pack);
    passed = require(restored_pack == 8 && !copy_probe::run(p, true, noop, &calls).attempted,
        "P1 did not restore readback state and disable after failure") && passed;
    // Offset, partial scene with nonuniform pixels outside all four edges.
    const auto gen_fbo = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(wglGetProcAddress("glGenFramebuffers"));
    const auto bind_fbo = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(wglGetProcAddress("glBindFramebuffer"));
    const auto attach = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(wglGetProcAddress("glFramebufferTexture2D"));
    const auto delete_fbo = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(wglGetProcAddress("glDeleteFramebuffers"));
    if (!gen_fbo || !bind_fbo || !attach || !delete_fbo) return 77;
    GLuint stage_texture = 0, stage_framebuffer = 0;
    bind_buffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glGenTextures(1, &stage_texture); glBindTexture(GL_TEXTURE_2D, stage_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 40, 24, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gen_fbo(1, &stage_framebuffer); bind_fbo(GL_FRAMEBUFFER, stage_framebuffer);
    attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, stage_texture, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    bind_fbo(GL_FRAMEBUFFER, 0); glDrawBuffer(GL_BACK); glReadBuffer(GL_BACK);
    glBindTexture(GL_TEXTURE_2D, original_texture); bind_buffer(GL_PIXEL_UNPACK_BUFFER, buffers[1]);
    p.world_rect[0] = p.viewport[0] = 7; p.world_rect[1] = p.viewport[1] = 5;
    p.world_rect[2] = p.viewport[2] = p.color_extent[0] = p.depth_extent[0] = 40;
    p.world_rect[3] = p.viewport[3] = p.color_extent[1] = p.depth_extent[1] = 24;
    struct stage_data { PFNGLBINDFRAMEBUFFERPROC bind; GLuint fbo; unsigned calls = 0; } stage_data{bind_fbo, stage_framebuffer};
    const auto bind_stage = [](void *opaque) {
        auto &d = *static_cast<struct stage_data *>(opaque);
        d.bind(GL_FRAMEBUFFER, d.fbo); glReadBuffer(GL_COLOR_ATTACHMENT0); glDrawBuffer(GL_COLOR_ATTACHMENT0);
    };
    const auto stage_noop = [](void *opaque) { ++static_cast<struct stage_data *>(opaque)->calls; };
    SetEnvironmentVariableA("FS_SCENE_WORLD_PROBE", "1");
    passed = require(copy_probe::valid_scene(p, true) && !copy_probe::full_scene(p, true), "partial scene gate failed") && passed;
    p.world_rect[0] = p.viewport[0] = 30;
    passed = require(!copy_probe::run_world(p, true, bind_stage, stage_noop, &stage_data).attempted,
        "overrunning world rectangle attempted GPU work") && passed;
    p.world_rect[0] = p.viewport[0] = 7;
    const auto world = copy_probe::run_world(p, true, bind_stage, stage_noop, &stage_data);
    std::printf("world: %s capture=%d output=%d outside=%d color=%d depth=%d stencil=%d state=%d\n", world.status,
        world.stage_equal, world.stage_output_equal, world.outside_equal, world.color_equal, world.depth_equal, world.stencil_equal, world.state_equal);
    passed = require(stage_data.calls == 1 && world.attempted && world.composited && world.stage_equal && world.stage_output_equal &&
        world.color_equal && world.outside_equal && world.depth_equal && world.stencil_equal && world.state_equal,
        "partial world roundtrip failed") && passed;
    const auto corrupt_stage = [](void *opaque) {
        const auto &d = *static_cast<struct stage_data *>(opaque);
        GLfloat clear[4]; glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
        const bool scissor = glIsEnabled(GL_SCISSOR_TEST) != 0;
        d.bind(GL_FRAMEBUFFER, d.fbo); glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glDisable(GL_SCISSOR_TEST); glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(clear[0], clear[1], clear[2], clear[3]); if (scissor) glEnable(GL_SCISSOR_TEST);
        d.bind(GL_FRAMEBUFFER, 0); glDrawBuffer(GL_BACK); glReadBuffer(GL_BACK);
    };
    const auto rejected = copy_probe::run_world(p, true, bind_stage, corrupt_stage, &stage_data);
    passed = require(rejected.attempted && !rejected.stage_output_equal && !rejected.composited && rejected.color_equal &&
        rejected.outside_equal && rejected.depth_equal && rejected.stencil_equal && rejected.state_equal &&
        !copy_probe::run_world(p, true, bind_stage, stage_noop, &stage_data).attempted,
        "bad staged output was not rejected without changing presentation") && passed;
    delete_fbo(1, &stage_framebuffer); glDeleteTextures(1, &stage_texture);
    GLuint source_depth = 0;
    std::vector<float> source_values(64 * 48);
    for (size_t i = 0; i < source_values.size(); ++i) source_values[i] = float(i) / float(source_values.size() - 1);
    bind_buffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glGenTextures(1, &source_depth); glBindTexture(GL_TEXTURE_2D, source_depth);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F, 64, 48, 0, GL_DEPTH_COMPONENT, GL_FLOAT, source_values.data());
    glBindTexture(GL_TEXTURE_2D, original_texture); bind_buffer(GL_PIXEL_UNPACK_BUFFER, buffers[1]);
    glPixelStorei(GL_PACK_IMAGE_HEIGHT, 99); glPixelStorei(GL_PACK_SKIP_IMAGES, 2);
    glPixelStorei(GL_PACK_SWAP_BYTES, GL_TRUE); glPixelStorei(GL_PACK_LSB_FIRST, GL_TRUE);
    const auto depth_sample = copy_probe::read_depth(source_depth, 64, 48);
    passed = require(depth_sample.valid && depth_sample.state_equal && depth_sample.count == 3072 &&
        depth_sample.minimum == 0 && depth_sample.maximum == 1 && depth_sample.values[0] == 0 &&
        depth_sample.values[1024] == source_values[8 * 64 + 16] && depth_sample.values.back() == 1,
        "original depth tile readback, ordering or state preservation failed") && passed;
    const auto repeat_depth = copy_probe::read_depth(source_depth, 64, 48);
    passed = require(repeat_depth.valid && repeat_depth.hash == depth_sample.hash &&
        !copy_probe::read_depth(source_depth, 65, 48).valid && !copy_probe::read_depth(original_texture, 64, 48).valid,
        "depth readback hash or rejection checks failed") && passed;
    GLint image_height = 0, skip_images = 0, swap_bytes = 0, lsb_first = 0;
    glGetIntegerv(GL_PACK_IMAGE_HEIGHT, &image_height); glGetIntegerv(GL_PACK_SKIP_IMAGES, &skip_images);
    glGetIntegerv(GL_PACK_SWAP_BYTES, &swap_bytes); glGetIntegerv(GL_PACK_LSB_FIRST, &lsb_first);
    passed = require(image_height == 99 && skip_images == 2 && swap_bytes == GL_TRUE && lsb_first == GL_TRUE,
        "depth readback did not restore additional packing state") && passed;
    glDeleteTextures(1, &source_depth);
    // Actual composition path: allow changed neural output, restore outside pixels,
    // and roll back the whole presentation if the chain reports incomplete work.
    glPixelStorei(GL_PACK_IMAGE_HEIGHT, 0); glPixelStorei(GL_PACK_SKIP_IMAGES, 0);
    glPixelStorei(GL_PACK_SWAP_BYTES, GL_FALSE); glPixelStorei(GL_PACK_LSB_FIRST, GL_FALSE);
    bind_buffer(GL_PIXEL_PACK_BUFFER, 0); glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0); glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    std::vector<unsigned char> presentation_before(64 * 48 * 4), presentation_after(presentation_before.size());
    std::vector<float> presentation_depth(64 * 48), presentation_depth_after(64 * 48);
    glReadPixels(0, 0, 64, 48, GL_RGBA, GL_UNSIGNED_BYTE, presentation_before.data());
    glReadPixels(0, 0, 64, 48, GL_DEPTH_COMPONENT, GL_FLOAT, presentation_depth.data());
    GLuint processing_textures[2] = {}, processing_fbos[2] = {};
    glGenTextures(2, processing_textures); gen_fbo(2, processing_fbos); bind_buffer(GL_PIXEL_UNPACK_BUFFER, 0);
    for (unsigned i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, processing_textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 48, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        bind_fbo(GL_FRAMEBUFFER, processing_fbos[i]); attach(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, processing_textures[i], 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    }
    bind_fbo(GL_FRAMEBUFFER, 0); glDrawBuffer(GL_BACK); glReadBuffer(GL_BACK);
    glBindTexture(GL_TEXTURE_2D, original_texture); bind_buffer(GL_PIXEL_UNPACK_BUFFER, buffers[1]);
    struct processing_data { PFNGLBINDFRAMEBUFFERPROC bind; GLuint scene, backup; bool complete = true; } processing{bind_fbo, processing_fbos[0], processing_fbos[1]};
    const auto processing_scene = [](void *opaque) {
        const auto &d = *static_cast<processing_data *>(opaque); d.bind(GL_FRAMEBUFFER, d.scene);
        glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    };
    const auto processing_backup = [](void *opaque) {
        const auto &d = *static_cast<processing_data *>(opaque); d.bind(GL_FRAMEBUFFER, d.backup);
        glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    };
    const auto processing_effect = [](void *opaque) -> bool {
        const auto &d = *static_cast<processing_data *>(opaque); GLfloat clear[4]; glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
        const bool scissor = glIsEnabled(GL_SCISSOR_TEST) != 0; glDisable(GL_SCISSOR_TEST);
        glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); // unexpected default-target write must be restored
        d.bind(GL_FRAMEBUFFER, d.scene); glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glClearColor(0, 1, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
        glClearColor(clear[0], clear[1], clear[2], clear[3]); if (scissor) glEnable(GL_SCISSOR_TEST);
        d.bind(GL_FRAMEBUFFER, 0); glDrawBuffer(GL_BACK); glReadBuffer(GL_BACK); return d.complete;
    };
    passed = require(copy_probe::process_world(p, processing_scene, processing_backup, processing_effect, &processing),
        "GPU processing-domain composition failed") && passed;
    glReadPixels(0, 0, 64, 48, GL_RGBA, GL_UNSIGNED_BYTE, presentation_after.data());
    glReadPixels(0, 0, 64, 48, GL_DEPTH_COMPONENT, GL_FLOAT, presentation_depth_after.data());
    bool outside_preserved = true;
    for (int row = 0; row < 48; ++row) for (int column = 0; column < 64; ++column) {
        const auto at = (size_t(row) * 64 + size_t(column)) * 4;
        if (column >= 7 && column < 47 && row >= 5 && row < 29) {
            outside_preserved = outside_preserved && presentation_after[at] == 0 && presentation_after[at + 1] == 255 && presentation_after[at + 2] == 255;
        } else for (unsigned channel = 0; channel < 4; ++channel)
            outside_preserved = outside_preserved && presentation_before[at + channel] == presentation_after[at + channel];
    }
    passed = require(outside_preserved && presentation_depth == presentation_depth_after,
        "processed scene output, outside pixels or presentation depth incorrect") && passed;
    presentation_before = presentation_after; processing.complete = false;
    passed = require(!copy_probe::process_world(p, processing_scene, processing_backup, processing_effect, &processing),
        "incomplete GPU chain was accepted") && passed;
    glReadPixels(0, 0, 64, 48, GL_RGBA, GL_UNSIGNED_BYTE, presentation_after.data());
    passed = require(presentation_before == presentation_after, "incomplete GPU chain did not restore presentation") && passed;
    delete_fbo(2, processing_fbos); glDeleteTextures(2, processing_textures);
    // Do not consume viewer errors in production; this test owns its context/errors.
    passed = require(glGetError() == GL_NO_ERROR, "native GL test generated an error") && passed;
    delete_buffers(2, buffers); glDeleteTextures(1, &original_texture);
    wglMakeCurrent(nullptr, nullptr); wglDeleteContext(context); ReleaseDC(window, dc); DestroyWindow(window);
    return passed ? 0 : 1;
}
