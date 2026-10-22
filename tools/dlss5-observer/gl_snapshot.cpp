#include <Windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include "gl_snapshot.hpp"
#include <cstring>

namespace gl_snapshot {
snapshot capture()
{
    snapshot s;
    GLint major = 0, minor = 0, profile = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major); glGetIntegerv(GL_MINOR_VERSION, &minor);
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
    if (profile != GL_CONTEXT_CORE_PROFILE_BIT || major < 4 || (major == 4 && minor < 5)) return s;
    const auto integer_i = reinterpret_cast<PFNGLGETINTEGERI_VPROC>(wglGetProcAddress("glGetIntegeri_v"));
    const auto integer64_i = reinterpret_cast<PFNGLGETINTEGER64I_VPROC>(wglGetProcAddress("glGetInteger64i_v"));
    const auto boolean_i = reinterpret_cast<PFNGLGETBOOLEANI_VPROC>(wglGetProcAddress("glGetBooleani_v"));
    const auto enabled_i = reinterpret_cast<PFNGLISENABLEDIPROC>(wglGetProcAddress("glIsEnabledi"));
    if (!integer_i || !integer64_i || !boolean_i || !enabled_i) return s;
    s.values.reserve(700);
    const auto ints = [&](GLenum name, unsigned count = 1) {
        GLint data[4] = {}; glGetIntegerv(name, data);
        for (unsigned i = 0; i < count; ++i) s.values.push_back(data[i]);
    };
    const auto floats = [&](GLenum name, unsigned count) {
        GLfloat data[4] = {}; glGetFloatv(name, data);
        for (unsigned i = 0; i < count; ++i) { uint32_t bits; std::memcpy(&bits, &data[i], sizeof(bits)); s.values.push_back(bits); }
    };
    for (const GLenum name : {GL_COPY_READ_BUFFER_BINDING, GL_COPY_WRITE_BUFFER_BINDING, GL_VERTEX_ARRAY_BINDING,
        GL_ARRAY_BUFFER_BINDING, GL_ELEMENT_ARRAY_BUFFER_BINDING, GL_CURRENT_PROGRAM, GL_UNIFORM_BUFFER_BINDING,
        GL_ACTIVE_TEXTURE, GL_READ_FRAMEBUFFER_BINDING, GL_DRAW_FRAMEBUFFER_BINDING, GL_READ_BUFFER,
        GL_CULL_FACE_MODE, GL_FRONT_FACE, GL_DEPTH_FUNC, GL_STENCIL_VALUE_MASK, GL_STENCIL_WRITEMASK,
        GL_STENCIL_REF, GL_STENCIL_FUNC, GL_STENCIL_PASS_DEPTH_PASS, GL_STENCIL_FAIL, GL_STENCIL_PASS_DEPTH_FAIL,
        GL_STENCIL_BACK_VALUE_MASK, GL_STENCIL_BACK_WRITEMASK, GL_STENCIL_BACK_REF, GL_STENCIL_BACK_FUNC,
        GL_STENCIL_BACK_PASS_DEPTH_PASS, GL_STENCIL_BACK_FAIL, GL_STENCIL_BACK_PASS_DEPTH_FAIL,
        GL_CLIP_ORIGIN, GL_CLIP_DEPTH_MODE, GL_LOGIC_OP_MODE, GL_PIXEL_PACK_BUFFER_BINDING, GL_PIXEL_UNPACK_BUFFER_BINDING,
        GL_PACK_ALIGNMENT, GL_PACK_ROW_LENGTH, GL_PACK_SKIP_PIXELS, GL_PACK_SKIP_ROWS,
        GL_PACK_IMAGE_HEIGHT, GL_PACK_SKIP_IMAGES, GL_PACK_SWAP_BYTES, GL_PACK_LSB_FIRST,
        GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_PIXELS, GL_UNPACK_SKIP_ROWS}) ints(name);
    ints(GL_VIEWPORT, 4); ints(GL_SCISSOR_BOX, 4); ints(GL_POLYGON_MODE, 2);
    floats(GL_BLEND_COLOR, 4);
    GLboolean depth_mask = GL_FALSE; glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask); s.values.push_back(depth_mask);
    for (const GLenum name : {GL_FRAMEBUFFER_SRGB, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_COLOR_LOGIC_OP, GL_CULL_FACE,
        GL_DEPTH_CLAMP, GL_SCISSOR_TEST, GL_MULTISAMPLE, GL_LINE_SMOOTH, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_DITHER})
        s.values.push_back(glIsEnabled(name));
    for (GLuint i = 0; i < 32; ++i) {
        for (const GLenum name : {GL_SAMPLER_BINDING, GL_TEXTURE_BINDING_2D}) {
            GLint value = 0; integer_i(name, i, &value); s.values.push_back(value);
        }
    }
    for (GLuint i = 0; i < 4; ++i) {
        GLint value = 0; integer_i(GL_UNIFORM_BUFFER_BINDING, i, &value); s.values.push_back(value);
        for (const GLenum name : {GL_UNIFORM_BUFFER_START, GL_UNIFORM_BUFFER_SIZE}) {
            GLint64 range = 0; integer64_i(name, i, &range); s.values.push_back(range);
        }
    }
    for (GLuint i = 0; i < 8; ++i) {
        ints(GL_DRAW_BUFFER0 + i);
        s.values.push_back(enabled_i(GL_BLEND, i));
        for (const GLenum name : {GL_BLEND_SRC_RGB, GL_BLEND_DST_RGB, GL_BLEND_SRC_ALPHA, GL_BLEND_DST_ALPHA,
            GL_BLEND_EQUATION_RGB, GL_BLEND_EQUATION_ALPHA}) {
            GLint value = 0; integer_i(name, i, &value); s.values.push_back(value);
        }
        GLboolean mask[4] = {}; boolean_i(GL_COLOR_WRITEMASK, i, mask);
        for (const auto value : mask) s.values.push_back(value);
    }
    GLint sample_mask = 0; integer_i(GL_SAMPLE_MASK_VALUE, 0, &sample_mask); s.values.push_back(sample_mask);
    s.valid = true;
    return s;
}
}
