// Native GE replay diagnostic. Inputs are extracted display-list records,
// never a guest CPU execution path. No disc or dump is bundled with this tool.
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <charconv>
#include "ge.hpp"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <fstream>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <array>
#include <vector>
#include <filesystem>
#include <chrono>
#include <algorithm>
#include <cstdlib>
using u32 = std::uint32_t;
std::vector<std::uint8_t> read(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("open: " + p.string());
    const auto length = f.tellg();
    if (length < 0 || length > 64 * 1024 * 1024)
        throw std::runtime_error("input exceeds replay size limit");
    f.seekg(0);
    return {std::istreambuf_iterator<char>(f), {}};
}
u32 word(const std::vector<std::uint8_t> &b, size_t p) {
    if (p + 4 > b.size())
        throw std::runtime_error("truncated input");
    return b[p] | (u32(b[p + 1]) << 8) | (u32(b[p + 2]) << 16) | (u32(b[p + 3]) << 24);
}
struct Context {
    EGLDisplay d;
    EGLContext c;
    EGLSurface s;
    Context() {
        d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        EGLint maj, min, n;
        EGLConfig config;
        if (!eglInitialize(d, &maj, &min) || !eglBindAPI(EGL_OPENGL_ES_API))
            throw std::runtime_error("EGL init");
        const EGLint a[] = {EGL_RENDERABLE_TYPE,
                            EGL_OPENGL_ES3_BIT,
                            EGL_SURFACE_TYPE,
                            EGL_PBUFFER_BIT,
                            EGL_RED_SIZE,
                            8,
                            EGL_GREEN_SIZE,
                            8,
                            EGL_BLUE_SIZE,
                            8,
                            EGL_ALPHA_SIZE,
                            8,
                            EGL_NONE};
        const EGLint ca[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_NONE},
                     sa[] = {EGL_WIDTH, 480, EGL_HEIGHT, 272, EGL_NONE};
        if (!eglChooseConfig(d, a, &config, 1, &n) || n != 1)
            throw std::runtime_error("EGL config");
        std::cout << "EGL channel bits:";
        for (EGLint attr : {EGL_RED_SIZE, EGL_GREEN_SIZE, EGL_BLUE_SIZE, EGL_ALPHA_SIZE}) {
            EGLint bits = 0;
            if (!eglGetConfigAttrib(d, config, attr, &bits) || bits < 8)
                throw std::runtime_error("RGBA8 context required");
            std::cout << " " << bits;
        }
        std::cout << "\n";
        c = eglCreateContext(d, config, EGL_NO_CONTEXT, ca);
        s = eglCreatePbufferSurface(d, config, sa);
        if (c == EGL_NO_CONTEXT || s == EGL_NO_SURFACE || !eglMakeCurrent(d, s, s, c))
            throw std::runtime_error("EGL context");
        std::cout << "GL renderer: " << glGetString(GL_RENDERER) << "\n";
    }
    ~Context() {
        eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(d, s);
        eglDestroyContext(d, c);
        eglTerminate(d);
    }
};
int main(int argc, char **argv) {
    try {
        if (argc < 4)
            throw std::runtime_error("usage: pspweb_ge_replay commands.bin pushbuffer.bin output.ppm [--frames N] "
                                     "[--warmup N] [--raw-display path] [--no-eco]");
        unsigned frameCount = 32, warmup = 8;
        bool noEco = false;
        std::filesystem::path rawDisplay;
        auto natural = [](const char *text) {
            unsigned value{};
            const char *end = text + std::strlen(text);
            auto result = std::from_chars(text, end, value);
            if (result.ec != std::errc{} || result.ptr != end)
                throw std::runtime_error("invalid integer option");
            return value;
        };
        for (int i = 4; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--no-eco")
                noEco = true;
            else if (option == "--frames" && i + 1 < argc)
                frameCount = natural(argv[++i]);
            else if (option == "--warmup" && i + 1 < argc)
                warmup = natural(argv[++i]);
            else if (option == "--raw-display" && i + 1 < argc)
                rawDisplay = argv[++i];
            else
                throw std::runtime_error("unknown or incomplete option: " + option);
        }
        if (frameCount == 0 || frameCount > 120 || warmup >= frameCount)
            throw std::runtime_error("require 1..120 frames and warmup less than frames");
#ifdef _WIN32
        struct PowerState {
            unsigned version, control, state;
        };
        PowerState power{1, 0, 0};
        const BOOL gotPower = GetProcessInformation(GetCurrentProcess(), static_cast<PROCESS_INFORMATION_CLASS>(4),
                                                    &power, sizeof(power));
        std::cout << "power_initial " << gotPower << " " << power.control << " " << power.state << "\n";
        if (noEco) {
            if (!gotPower)
                throw std::runtime_error("cannot inspect process power throttling");
            power.control |= 1;
            power.state &= ~1u;
            if (!SetProcessInformation(GetCurrentProcess(), static_cast<PROCESS_INFORMATION_CLASS>(4), &power,
                                       sizeof(power)))
                throw std::runtime_error("cannot disable process power throttling");
            std::cout << "power_disabled_for_this_process 1\n";
        }
#else
        if (noEco)
            throw std::runtime_error("--no-eco requires Windows");
#endif
        auto records = read(argv[1]), push = read(argv[2]);
        if (records.size() % 9 || push.size() > 32 * 1024 * 1024)
            throw std::runtime_error("invalid sizes");
        Context ctx;
        psprecomp::GuestMemory mem(64u * 1024u * 1024u);
        pspweb::Ge ge(mem);
        pspweb::GeGl gl(mem);
        if (!gl.init())
            throw std::runtime_error("renderer init");
        ge.set_gl(&gl);
        std::cout << "guest_ram_mib 64\n";
        constexpr u32 base = 0x09000000, list = 0x08800000;
        std::memcpy(mem.raw_pointer(base, push.size()), push.data(), push.size());
        auto run = [&](const std::vector<u32> &ops) {
            if (ops.size() > 256 * 1024)
                throw std::runtime_error("oversized batch");
            auto ptr = mem.raw_pointer(list, (ops.size() + 1) * 4);
            if (!ptr)
                throw std::runtime_error("invalid replay list range");
            std::memcpy(ptr, ops.data(), ops.size() * 4);
            mem.store32(list + u32(ops.size() * 4), 0x0c000000);
            if (!ge.run_list(list, 0))
                throw std::runtime_error("unexpected stall");
        };
        auto command = [](u32 op, u32 value) { return op << 24 | (value & 0xffffff); };
        std::vector<double> frameTimes;
        for (unsigned iteration = 0; iteration < frameCount; ++iteration) {
            auto frameStart = std::chrono::steady_clock::now();
            std::array<u32, 8> tex{}, width{};
            u32 clutAddress = 0, clutFlags = 0, display = 0, displayStride = 0, displayFormat = 0;
            size_t count = 0;
            for (size_t i = 0; i < records.size(); i += 9, ++count) {
                u32 type = records[i], size = word(records, i + 1), ptr = word(records, i + 5);
                if (ptr > push.size() || size > push.size() - ptr)
                    throw std::runtime_error("payload bounds");
                std::vector<u32> ops;
                switch (type) {
                case 0: {
                    if (size != 2048)
                        throw std::runtime_error("unsupported context size");
                    bool ended = false;
                    for (u32 j = 17 * 4; j < size; j += 4) {
                        u32 op = word(push, ptr + j);
                        if (op == 0x0c000000) {
                            ended = true;
                            break;
                        }
                        if ((op >> 24) == 4)
                            throw std::runtime_error("unsupported legacy context");
                        ops.push_back(op);
                    }
                    if (!ended)
                        throw std::runtime_error("context has no END");
                    run(ops);
                    break;
                }
                case 1:
                    if (size % 4)
                        throw std::runtime_error("unaligned registers");
                    for (u32 j = 0; j < size; j += 4) {
                        u32 op = word(push, ptr + j), cmd = op >> 24;
                        if (cmd >= 0xa0 && cmd <= 0xa7)
                            continue;
                        if (cmd >= 0xa8 && cmd <= 0xaf) {
                            u32 level = cmd - 0xa8;
                            width[level] = op & 0xffff;
                            op = command(cmd, width[level] | ((tex[level] >> 8) & 0xff0000));
                        }
                        ops.push_back(op);
                    }
                    run(ops);
                    break;
                case 2:
                case 3:
                    run({command(0x13, 0), command(0x10, (base + ptr) >> 8 & 0x0f0000),
                         command(type == 2 ? 1 : 2, (base + ptr) & 0xffffff)});
                    break;
                case 4:
                    if (clutAddress) {
                        gl.flush();
                        auto out = mem.raw_pointer(clutAddress, size);
                        if (!out)
                            throw std::runtime_error("invalid CLUT address");
                        std::memcpy(out, push.data() + ptr, size);
                        ge.touch_vram(clutAddress, size);
                        clutAddress = 0;
                    } else {
                        run({command(0xb0, base + ptr), command(0xb1, (base + ptr) >> 8 & 0x0f0000),
                             command(0xc4, size / 32)});
                    }
                    break;
                case 9:
                    if (size != 12)
                        throw std::runtime_error("invalid display");
                    display = word(push, ptr);
                    displayStride = word(push, ptr + 4);
                    displayFormat = word(push, ptr + 8);
                    break;
                case 10:
                    if (size != 8)
                        throw std::runtime_error("invalid CLUT record");
                    clutAddress = word(push, ptr);
                    clutFlags = word(push, ptr + 4);
                    break;
                default:
                    if (type >= 16 && type < 24) {
                        u32 level = type - 16;
                        tex[level] = base + ptr;
                        run({command(0xa0 + level, tex[level]),
                             command(0xa8 + level, width[level] | (tex[level] >> 8 & 0x0f0000))});
                    } else if (type >= 24 && type < 32) {
                        if (size < 16)
                            throw std::runtime_error("invalid framebuffer record");
                        u32 level = type - 24, addr = word(push, ptr), stride = word(push, ptr + 4),
                            flags = word(push, ptr + 8);
                        tex[level] = addr;
                        width[level] = stride;
                        if (!(flags & 2)) {
                            gl.flush();
                            auto out = mem.raw_pointer(addr, size - 16);
                            if (!out)
                                throw std::runtime_error("invalid framebuffer address");
                            std::memcpy(out, push.data() + ptr + 16, size - 16);
                            ge.touch_vram(addr, size - 16);
                        }
                        run({command(0xa0 + level, addr), command(0xa8 + level, stride | (addr >> 8 & 0x0f0000))});
                    } else
                        throw std::runtime_error("unsupported record " + std::to_string(type));
                }
            }
            if (!display || displayStride < 480 || displayStride > 1024 || displayFormat > 3)
                throw std::runtime_error("missing display");
            gl.present(display, displayStride, displayFormat, 0, 480, 272);
            // Retire the EGL frame as a native presentation would. Pbuffers
            // retain their pixels; this also permits driver frame cleanup.
            if (!eglSwapBuffers(ctx.d, ctx.s))
                throw std::runtime_error("EGL frame completion");
            glFinish();
            frameTimes.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count());
            if (iteration + 1 < frameCount) {
                ge.next_frame();
                ge.reset_stats();
                gl.next_frame();
                gl.reset_counters();
                gl.reset_passes();
                continue;
            }

            std::vector<std::uint8_t> pixels(480 * 272 * 4);
            glReadPixels(0, 0, 480, 272, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            if (!rawDisplay.empty()) {
                if (displayStride < 480 || displayStride > 1024 || displayFormat > 3)
                    throw std::runtime_error("invalid display layout");
                u32 bytes = displayStride * 272u * (displayFormat == 3 ? 4u : 2u);
                gl.prepare_vram_read(display, bytes);
                const auto *raw = mem.raw_pointer(display, bytes);
                if (!raw)
                    throw std::runtime_error("invalid display range");
                std::ofstream output(rawDisplay, std::ios::binary);
                output.write(reinterpret_cast<const char *>(raw), bytes);
                if (!output)
                    throw std::runtime_error("raw display output failed");
            }
            if (glGetError() != GL_NO_ERROR)
                throw std::runtime_error("GL readback");
            std::ofstream output(argv[3], std::ios::binary);
            output << "P6\n480 272\n255\n";
            for (int y = 271; y >= 0; --y)
                for (int x = 0; x < 480; ++x)
                    output.write(reinterpret_cast<char *>(pixels.data() + (y * 480 + x) * 4), 3);
            if (!output)
                throw std::runtime_error("output failed");
            std::cout << "records " << count << " prims " << ge.stats().prims << " triangles " << ge.stats().triangles
                      << " draws " << gl.draw_calls() << " downloads " << gl.downloads() << " uploads " << gl.uploads()
                      << "\n";
            gl.report_targets();

            std::cout << "blend_copies " << gl.passes().blend_copies << " blend_pixels " << gl.passes().blend_pixels
                      << "\n";
            std::cout << "alpha_resolves " << gl.passes().alpha_resolves << " stencil_resolves "
                      << gl.passes().stencil_resolves << " feedback_copies " << gl.passes().feedback_copies << "\n";
        }
        std::cout << "plane_upload_bytes_total " << gl.planes().upload_bytes << "\n";
        std::cout << "flush_ms " << gl.flush_ms() << "\n";
        std::cout << "frame_ms [";
        for (size_t i = 0; i < frameTimes.size(); ++i)
            std::cout << (i ? "," : "") << frameTimes[i];
        std::cout << "]\n";
        std::vector<double> steady(frameTimes.begin() + warmup, frameTimes.end());
        std::sort(steady.begin(), steady.end());
        const auto middle = steady.size() / 2;
        const double median = steady.size() % 2 ? steady[middle] : (steady[middle - 1] + steady[middle]) / 2;
        std::cout << "warmup_frames " << warmup << " steady_median_ms " << median << " steady_max_ms " << steady.back()
                  << "\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
