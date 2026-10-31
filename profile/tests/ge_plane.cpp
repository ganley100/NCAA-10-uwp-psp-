// Asset-free checks of the production GE plane helpers and GPU record lifetime.
#include "ge_gl.hpp"
#include "ge_plane_math.hpp"
#include "ge_plane_shader.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <array>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Word = std::array<std::uint32_t, 4>;
constexpr unsigned count = 4096, width = 256, height = count / width;
unsigned seed = 813;
unsigned next() { return seed = seed * 1664525u + 1013904223u; }
unsigned bits(float v) { return std::bit_cast<unsigned>(v); }
float number(unsigned v) { return std::bit_cast<float>(v); }

struct Context {
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLContext context{};
    EGLSurface surface{};
    Context() {
        EGLint major, minor, n; EGLConfig config;
        assert(eglInitialize(display, &major, &minor) && eglBindAPI(EGL_OPENGL_ES_API));
        const EGLint attrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        assert(eglChooseConfig(display, attrs, &config, 1, &n) && n == 1);
        const EGLint ctx[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE};
        const EGLint pbuf[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, ctx);
        surface = eglCreatePbufferSurface(display, config, pbuf);
        assert(context != EGL_NO_CONTEXT && surface != EGL_NO_SURFACE);
        assert(eglMakeCurrent(display, surface, surface, context));
    }
    ~Context() {
        eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(display, surface); eglDestroyContext(display, context); eglTerminate(display);
    }
};

GLuint shader(GLenum kind, const std::string &source) {
    GLuint id = glCreateShader(kind); const char *text = source.c_str();
    glShaderSource(id, 1, &text, nullptr); glCompileShader(id);
    GLint ok; glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
    if (!ok) { std::array<char, 8192> log{}; glGetShaderInfoLog(id, log.size(), nullptr, log.data()); std::cerr << log.data(); }
    assert(ok); return id;
}

GLuint program(bool fast) {
    const std::string fragment = std::string("#version 300 es\nprecision highp float;\nprecision highp int;\n") +
        (fast ? "#define PSPWEB_FAST_COVERAGE 1\n" : "") + pspweb::ge_plane::shader_math + R"glsl(
uniform highp usampler2D u_input;
uniform int u_mode;
layout(location=0) out uvec4 result;
uvec4 input_word(int i) { return texelFetch(u_input, ivec2(i & 255, i >> 8), 0); }
void main() {
    int index = int(gl_FragCoord.x) + int(gl_FragCoord.y) * 256;
    uvec4 a = input_word(index * 2), b = input_word(index * 2 + 1);
    result = uvec4(0u);
    if (u_mode == 0) result.x = floatBitsToUint(plane_recip(uintBitsToFloat(a.x)));
    if (u_mode == 1) result.x = floatBitsToUint(plane_product(uintBitsToFloat(a.x), uintBitsToFloat(a.y)));
    if (u_mode == 2) result.x = floatBitsToUint(plane_value(a, ivec2(b.xy)));
    if (u_mode == 3) result.x = plane_edge_inside(ivec2(a.xy), ivec2(a.zw), ivec2(b.xy)) ? 1u : 0u;
#ifndef PSPWEB_FAST_COVERAGE
    if (u_mode == 4) result.xy = plane_multiply(int(a.x), int(a.y));
#endif
}
)glsl";
    GLuint v = shader(GL_VERTEX_SHADER, R"glsl(#version 300 es
void main() { vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0, 1); }
)glsl"), f = shader(GL_FRAGMENT_SHADER, fragment), id = glCreateProgram();
    glAttachShader(id, v); glAttachShader(id, f); glLinkProgram(id);
    GLint ok; glGetProgramiv(id, GL_LINK_STATUS, &ok); assert(ok);
    glDeleteShader(v); glDeleteShader(f); return id;
}

void gpu_arithmetic() {
    GLuint textures[3], fbo, vao; glGenTextures(3, textures);
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    for (unsigned i = 0; i < 3; ++i) {
        glActiveTexture(GL_TEXTURE0 + i); glBindTexture(GL_TEXTURE_2D, textures[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32UI, width, height, 0, GL_RGBA_INTEGER, GL_UNSIGNED_INT, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[2], 0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    std::array<Word, 128> coefficients{};
    for (unsigned i = 0; i < coefficients.size(); ++i)
        coefficients[i] = {unsigned(pspweb::ge_math::reciprocal_segments[i].start),
                           unsigned(pspweb::ge_math::reciprocal_segments[i].slope), 0, 0};
    glActiveTexture(GL_TEXTURE1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32UI, 128, 1, 0, GL_RGBA_INTEGER, GL_UNSIGNED_INT, coefficients.data());
    glViewport(0, 0, width, height); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST); glDisable(GL_STENCIL_TEST);
    for (bool fast : {false, true}) {
        GLuint id = program(fast); glUseProgram(id);
        glUniform1i(glGetUniformLocation(id, "u_input"), 0);
        glUniform1i(glGetUniformLocation(id, "u_reciprocal"), 1);
        for (unsigned mode = 0; mode < (fast ? 4u : 5u); ++mode) {
            std::vector<Word> input(count * 2), expected(count), actual(count);
            for (unsigned i = 0; i < count; ++i) {
                auto &a = input[i * 2], &b = input[i * 2 + 1];
                if (mode == 0) {
                    a[0] = ((1 + next() % 254) << 23) | (next() & 0x807FFF00u);
                    expected[i][0] = bits(pspweb::ge_math::reciprocal(number(a[0])));
                } else if (mode == 1) {
                    a[0] = ((95 + next() % 64) << 23) | (next() & 0x807FFF00u);
                    a[1] = ((95 + next() % 64) << 23) | (next() & 0x807FFF00u);
                    expected[i][0] = bits(pspweb::ge_plane::uv_product(number(a[0]), number(a[1])));
                } else if (mode == 2) {
                    // Large cancelling base/products, with bounded covered values.
                    std::array<std::int64_t, 3> x{900000, 910000, 905000}, y{-910000, -910000, -900000};
                    std::array<float, 3> v{};
                    for (float &f : v) f = float(int(next() % 65535) - 32767);
                    const auto p = pspweb::ge_plane::shared(x, y, v);
                    const std::int64_t px = 905000, py = -906667;
                    a = {unsigned(p.base), unsigned(p.dx), unsigned(p.dy), unsigned(p.exponent)};
                    b = {unsigned(px), unsigned(py), 0, 0};
                    expected[i][0] = bits(pspweb::ge_plane::value(p, px, py));
                } else if (mode == 3) {
                    auto coordinate = [&] { return int(next() % (fast ? 8192u : 2097152u)) - (fast ? 4096 : 1048576); };
                    std::int64_t ax = coordinate(), ay = coordinate(), bx = coordinate(), by = coordinate();
                    std::int64_t px = coordinate(), py = coordinate();
                    // Include exact top-left boundaries, not only random interiors.
                    if (i % 16 == 0) { px = ax; py = ay; }
                    a = {unsigned(ax), unsigned(ay), unsigned(bx), unsigned(by)};
                    b = {unsigned(px), unsigned(py), 0, 0};
                    const auto cross = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
                    expected[i][0] = cross > 0 || (cross == 0 && (by < ay || (by == ay && bx > ax)));
                } else {
                    a = {next(), next(), 0, 0};
                    const std::int64_t product = std::int64_t(std::bit_cast<std::int32_t>(a[0])) * std::bit_cast<std::int32_t>(a[1]);
                    expected[i] = {unsigned(product), unsigned(std::uint64_t(product) >> 32), 0, 0};
                }
            }
            glActiveTexture(GL_TEXTURE0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32UI, width, height * 2, 0, GL_RGBA_INTEGER, GL_UNSIGNED_INT, input.data());
            glUniform1i(glGetUniformLocation(id, "u_mode"), mode); glDrawArrays(GL_TRIANGLES, 0, 3);
            glReadPixels(0, 0, width, height, GL_RGBA_INTEGER, GL_UNSIGNED_INT, actual.data());
            assert(glGetError() == GL_NO_ERROR);
            for (unsigned i = 0; i < count; ++i) {
                if (actual[i] != expected[i]) std::cerr << "GPU plane arithmetic: fast=" << fast << " mode=" << mode << " case=" << i
                    << " expected=" << expected[i][0] << " actual=" << actual[i][0] << '\n';
                assert(actual[i] == expected[i]);
            }
        }
        glDeleteProgram(id);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0); glBindVertexArray(0);
    glDeleteFramebuffers(1, &fbo); glDeleteTextures(3, textures); glDeleteVertexArrays(1, &vao);
    std::cout << "36,864 production GLSL arithmetic/edge cases passed\n";
}

void atlas_lifetime() {
    psprecomp::GuestMemory memory; pspweb::GeGl gl(memory); assert(gl.init());
    constexpr std::array<unsigned, 7> palette{0xFF0000FF, 0xFF00FF00, 0xFFFF0000, 0xFF00FFFF, 0xFFFF00FF, 0xFFFFFF00, 0xFFFFFFFF};
    pspweb::GlDrawState state{};
    state.fb_stride = 64; state.fb_format = 3; state.sx2 = 63; state.sy2 = 127;
    state.texture = state.through = true; state.tex_func = 3; state.lod_mode = 1;
    state.tex_width = 7; state.tex_height = 1; state.texels = palette.data();
    state.texture_identity = palette.data(); state.texture_hash = 1;
    const auto downloads = gl.downloads();
    std::uint64_t expected_upload_bytes = 0;
    for (unsigned i = 0; i < 6145; ++i) {
        state.env = i; gl.set_state(state);
        const float x = float(i % 64), y = float(i / 64), u = float(i % 7) + .5f;
        const pspweb::GlVertex a{x, y, 0, 1, u, .5f, 255, 255, 255, 255},
            b{x + 1, y, 0, 1, u, .5f, 255, 255, 255, 255}, c{x, y + 1, 0, 1, u, .5f, 255, 255, 255, 255},
            d{x + 1, y + 1, 0, 1, u, .5f, 255, 255, 255, 255};
        // Mix a partial row with a two-row record batch, including after atlas
        // reuse. Unused texels may contain old data and must never be sampled.
        const unsigned copies = i % 31 == 0 ? 8 : 1;
        for (unsigned j = 0; j < copies; ++j) {
            gl.triangle(a, b, c); gl.triangle(b, d, c);
        }
        expected_upload_bytes += (copies == 8 ? 128u : 10u) * sizeof(Word);
    }
    gl.flush();
    assert(gl.downloads() == downloads && gl.planes().atlas_rollovers >= 3);
    assert(gl.planes().upload_bytes == expected_upload_bytes);
    gl.prepare_vram_read(0x04000000, 64 * 128 * 4);
    for (unsigned i = 0; i < 6145; ++i) assert((memory.load32(0x04000000 + i * 4) & 0xFFFFFF) == (palette[i % 7] & 0xFFFFFF));
    assert(glGetError() == GL_NO_ERROR);
    std::cout << "Atlas rotation/reuse preserved 6,145 unique output pixels without rendering downloads\n";
}
} // namespace

int main() {
    Context context;
    gpu_arithmetic();
    atlas_lifetime();
}
