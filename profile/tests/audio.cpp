// Exercise the installed HLE handlers and real scheduler without a game image.
#include "kernel.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

using psprecomp::Runtime;
using Ctx = psprecomp::AllegrexContext;
namespace {
constexpr std::uint32_t reserve = 0x5EC81C55u, release = 0x6FC46853u;
constexpr std::uint32_t output = 0xE2D56B2Du, blocking = 0x13F592BCu;
constexpr std::uint32_t length = 0xCB2E439Eu, config = 0x95FD0C2Du, volume = 0xB7E1D8E7u;
constexpr std::uint32_t rest = 0xB011922Fu, rest_len = 0xE9D97901u;
constexpr std::uint32_t busy = 0x80260002u, invalid_channel = 0x80260003u;
constexpr std::uint32_t buf_a = 0x08900000u, buf_b = 0x08920000u;
std::vector<std::int16_t> pcm;
bool resumed{}, contended{};

void sink(const std::int16_t *samples, std::uint32_t frames) {
    pcm.insert(pcm.end(), samples, samples + frames * 2u);
}
std::uint32_t call(Runtime &rt, std::uint32_t nid, std::initializer_list<std::uint32_t> args,
                   const char *library = "sceAudio") {
    Ctx ctx{};
    auto reg = 4u;
    for (auto value : args) ctx.gpr[reg++] = value;
    rt.invoke_import(library, nid, ctx);
    return ctx.gpr[2];
}
void fill(Runtime &rt, std::uint32_t address, std::uint32_t frames, std::int16_t left, std::int16_t right) {
    for (std::uint32_t i = 0; i < frames; ++i) {
        rt.memory().store16(address + i * 4u, static_cast<std::uint16_t>(left));
        rt.memory().store16(address + i * 4u + 2u, static_cast<std::uint16_t>(right));
    }
}
bool contains_pair(std::int16_t left, std::int16_t right) {
    for (std::size_t i = 0; i + 1u < pcm.size(); i += 2u)
        if (pcm[i] == left && pcm[i + 1u] == right) return true;
    return false;
}
void check_volume_quantization() {
    // Exercise the installed output handler and final PCM sink. Use floating
    // point floor as an independent oracle for the driver's signed fixed-point
    // gain, including both clipping limits and independent stereo volumes.
    for (const auto gain : {0u, 1u, 0x4000u, 0x7FFFu, 0x8000u, 0x8001u, 0xFFFFu}) {
        for (int first : {-32768, 0}) {
            Runtime rt;
            pspweb::Kernel kernel(rt);
            kernel.install({});
            kernel.set_audio_sink(sink);
            kernel.frame(0.1); // Establish a nonzero guest sample clock.
            assert(call(rt, reserve, {0u, 32768u, 0u}) == 0u);
            pcm.clear();
            std::vector<std::int16_t> expected;
            expected.reserve(65536u);
            for (int i = 0; i < 32768; ++i) {
                const auto left = static_cast<std::int16_t>(first + i);
                const auto right = static_cast<std::int16_t>(-1 - left);
                rt.memory().store16(buf_a + i * 4u, static_cast<std::uint16_t>(left));
                rt.memory().store16(buf_a + i * 4u + 2u, static_cast<std::uint16_t>(right));
                for (auto [sample, volume] : {std::pair{left, gain}, std::pair{right, 0xFFFFu - gain}}) {
                    const auto scaled = static_cast<int>(std::floor(static_cast<double>(sample) * volume / 32768.0));
                    expected.push_back(static_cast<std::int16_t>(std::clamp(scaled, -32768, 32767)));
                }
            }
            assert(call(rt, output, {0u, gain, 0xFFFFu - gain, buf_a}) == 32768u);
            for (int i = 0; i < 50; ++i) kernel.frame(0.1);
            assert(call(rt, rest, {0u}) == 0u);
            // Ignore the surrounding idle silence, but require every output
            // sample in the submitted buffer to match, in order.
            const auto match = std::search(pcm.begin(), pcm.end(), expected.begin(), expected.end());
            if (match == pcm.end()) {
                std::cerr << "PCM mismatch: gain=" << gain << " first=" << first
                          << " captured=" << pcm.size() << " expected=" << expected.size() << '\n';
                for (std::size_t i = 0; i < std::min(pcm.size(), std::size_t{16}); ++i)
                    std::cerr << i << ": " << pcm[i] << " expected " << expected[i] << '\n';
            }
            assert(match != pcm.end());
        }
    }
}
void contender(Runtime &rt, Ctx &ctx) {
    assert(call(rt, blocking, {7u, 0x8000u, 0x8000u, buf_b}) == busy);
    assert(call(rt, release, {7u}) == busy);
    assert(call(rt, length, {7u, 1024u}) == busy);
    assert(call(rt, config, {7u, 0x10u}) == busy);
    assert(call(rt, rest, {7u}) > 2048u);
    contended = true;
    ctx.gpr[4] = 0u;
    rt.invoke_import("ThreadManForUser", 0xAA73C935u, ctx);
}
void producer(Runtime &rt, Ctx &ctx) {
    assert(call(rt, reserve, {0xFFFFFFFFu, 4096u, 0u}) == 7u);
    assert(call(rt, output, {7u, 0x8000u, 0x8000u, buf_a}) == 4096u);
    // Make a second thread ready, then park the producer on the occupied channel.
    rt.memory().store32(0x08806000u, 0x74736574u); // "test"
    rt.memory().store8(0x08806004u, 0u);
    auto uid = call(rt, 0x446D8DE6u, {0x08806000u, 0x08805000u, 0x30u, 0x4000u, 0u, 0u}, "ThreadManForUser");
    assert(static_cast<std::int32_t>(uid) > 0);
    assert(call(rt, 0xF475845Du, {uid, 0u, 0u}, "ThreadManForUser") == 0u);
    ctx.gpr[4] = 7u;
    ctx.gpr[5] = ctx.gpr[6] = 0x8000u;
    ctx.gpr[7] = buf_b;
    ctx.gpr[31] = 0x08804004u;
    rt.invoke_import("sceAudio", blocking, ctx);
}
void after_wait(Runtime &rt, Ctx &ctx) {
    assert(ctx.gpr[2] == 4096u); // DelayThread must not overwrite this with zero.
    assert(contended);
    resumed = true;
    ctx.gpr[4] = 0u;
    rt.invoke_import("ThreadManForUser", 0xAA73C935u, ctx);
}
}

int main() {
    check_volume_quantization();
    {
        Runtime rt;
        pspweb::Kernel kernel(rt);
        kernel.install({});
        kernel.set_audio_sink(sink);
        assert(call(rt, length, {0u, 512u}) == 0x80260001u);
        assert(call(rt, length, {0u, 1u}) == 0x80260006u);
        assert(call(rt, reserve, {8u, 1u, 1u}) == invalid_channel);
        assert(call(rt, reserve, {0xFFFFFFFFu, 65u, 0u}) == 0x80260006u);
        assert(call(rt, reserve, {0xFFFFFFFFu, 512u, 1u}) == 0x80260007u);
        assert(call(rt, volume, {99u, 0x10000u, 0u}) == 0x8026000Bu);
        assert(call(rt, volume, {0u, 0xFFFFFFFFu, 0u}) == 0u);
        assert(call(rt, reserve, {0xFFFFFFFFu, 16384u, 0u}) == 7u);
        assert(call(rt, reserve, {7u, 512u, 0u}) == invalid_channel);
        fill(rt, buf_a, 16384u, 1000, -2000);
        assert(call(rt, output, {7u, 0x8000u, 0x8000u, buf_a}) == 16384u);
        assert(call(rt, output, {7u, 0x8000u, 0x8000u, buf_b}) == busy);
        assert(call(rt, config, {7u, 0x10u}) == busy);
        assert(call(rt, volume, {7u, 0x4000u, 0xFFFFFFFFu}) == 0u);
        // Future blocks must read the updated guest memory and live volume.
        fill(rt, buf_a, 16384u, 3000, -4000);
        for (int i = 0; i < 30; ++i) kernel.frame(0.1);
        assert(contains_pair(1500, -4000));
        assert(call(rt, rest, {7u}) == 0u);
        assert(call(rt, config, {7u, 0x10u}) == 0u);
        assert(call(rt, length, {7u, 64u}) == 0u);
        assert(call(rt, blocking, {7u, 0xFFFFFFFFu, 0u, 0u}) == 0x8026000Bu);
        assert(call(rt, output, {7u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0u}) == 64u);
        assert(call(rt, rest, {7u}) == 0u && call(rt, rest_len, {7u}) == 64u);
        assert(call(rt, length, {7u, 1024u}) == 0u);
        for (unsigned i = 0; i < 1024u; ++i) rt.memory().store16(buf_a + i * 2u, 1000u);
        assert(call(rt, output, {7u, 0xFFFFFFFFu, 0xFFFFFFFFu, buf_a}) == 1024u);
        assert(call(rt, release, {7u}) == 0u);
        assert(call(rt, reserve, {0xFFFFFFFFu, 512u, 0u}) == 6u); // released buffer still plays
        assert(call(rt, release, {6u}) == 0u);
        for (int i = 0; i < 4; ++i) kernel.frame(0.1);
        assert(contains_pair(500, 1000)); // mono duplication, independently retained volumes
        assert(call(rt, release, {7u}) == 0x80260008u);
        for (int i = 7; i >= 0; --i) assert(call(rt, reserve, {0xFFFFFFFFu, 512u, 0u}) == static_cast<unsigned>(i));
        assert(call(rt, reserve, {0xFFFFFFFFu, 512u, 0u}) == 0x80260005u);
    }
    {
        pcm.clear();
        Runtime rt;
        pspweb::Kernel kernel(rt);
        kernel.install({});
        kernel.set_audio_sink(sink);
        fill(rt, buf_a, 1024u, -1, 1);
        fill(rt, buf_b, 1024u, 1, -1);
        assert(call(rt, reserve, {0u, 1024u, 0u}) == 0u);
        assert(call(rt, reserve, {1u, 1024u, 0u}) == 1u);
        assert(call(rt, output, {0u, 0x4000u, 0x4000u, buf_a}) == 1024u);
        assert(call(rt, output, {1u, 0x4000u, 0x4000u, buf_b}) == 1024u);
        for (int i = 0; i < 4; ++i) kernel.frame(0.1);
        // Each channel is quantized before accumulation. Opposite sub-LSB
        // samples do not cancel to zero as a single final rounding would.
        assert(contains_pair(-1, -1));
    }
    {
        pcm.clear();
        Runtime rt;
        pspweb::Kernel kernel(rt);
        kernel.install({});
        kernel.set_audio_sink(sink);
        fill(rt, buf_a, 1024u, 20000, -20000);
        fill(rt, buf_b, 1024u, 20000, -20000);
        assert(call(rt, reserve, {0u, 1024u, 0u}) == 0u);
        assert(call(rt, reserve, {1u, 1024u, 0u}) == 1u);
        assert(call(rt, output, {0u, 0x8000u, 0x8000u, buf_a}) == 1024u);
        assert(call(rt, output, {1u, 0x8000u, 0x8000u, buf_b}) == 1024u);
        for (int i = 0; i < 4; ++i) kernel.frame(0.1);
        assert(contains_pair(32767, -32768)); // mix in 32 bits, then clamp once
    }
    {
        pcm.clear();
        Runtime rt;
        pspweb::Kernel kernel(rt);
        kernel.install({});
        kernel.set_audio_sink(sink);
        fill(rt, buf_a, 4096u, 1100, -1100);
        fill(rt, buf_b, 4096u, 2200, -2200);
        rt.register_function(0x08804000u, producer, "producer");
        rt.register_function(0x08804004u, after_wait, "resumed");
        rt.register_function(0x08805000u, contender, "contender");
        kernel.boot(0x08804000u, 0u, "test");
        for (int i = 0; i < 20; ++i) kernel.frame(2.0);
        assert(!kernel.halted() && resumed && contended);
        assert(contains_pair(1100, -1100) && contains_pair(2200, -2200));
    }
    std::cout << "Audio HLE: full-range gain quantization, mix saturation, validation, live PCM/volume, allocation, null buffers, contention and scheduled returns passed\n";
}
