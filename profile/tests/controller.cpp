// Controller HLE behavior through the public imports, without game assets.
#include "kernel.hpp"
#include <cassert>
#include <iostream>
#include <array>

using psprecomp::Runtime;
namespace {
constexpr std::uint32_t peek = 0x3A622550u, set_mode = 0x1F4011E6u, get_mode = 0xDA6B76A1u;
constexpr std::uint32_t set_cycle = 0x6A2774F3u, get_cycle = 0x02BAAD91u;
constexpr std::uint32_t read = 0x1F803938u, negative_peek = 0xC152080Au, negative_read = 0x60B81F86u;
constexpr std::uint32_t peek_latch = 0xB1D0E5CDu, read_latch = 0x0B588501u;
constexpr std::uint32_t out = 0x08900000u;
std::uint32_t call(Runtime &rt, std::uint32_t nid, std::uint32_t a0, std::uint32_t a1 = 0u) {
    psprecomp::AllegrexContext ctx{};
    ctx.gpr[4] = a0; ctx.gpr[5] = a1;
    rt.invoke_import("sceCtrl", nid, ctx);
    return ctx.gpr[2];
}
void advance(pspweb::Kernel &kernel) {
    // No guest threads are needed to exercise the hardware sample clock.
    (void)kernel.frame(0.1);
    assert(!kernel.halted());
}
using Ctx = psprecomp::AllegrexContext;
unsigned wake_order;
std::uint32_t last_stamp, expected_spacing;
void park(Runtime &rt, Ctx &ctx, unsigned index) {
    ctx.gpr[4] = out + index * 64u;
    ctx.gpr[5] = index == 2u ? 0u : 8u; // Even a zero-count read waits for the next sample.
    ctx.gpr[31] = 0x08804100u + index * 4u;
    rt.invoke_import("sceCtrl", index == 0u ? negative_read : read, ctx);
}
void waiter_one(Runtime &rt, Ctx &ctx) { park(rt, ctx, 1u); }
void waiter_two(Runtime &rt, Ctx &ctx) { park(rt, ctx, 2u); }
void finished(Runtime &rt, Ctx &ctx, unsigned index) {
    assert(wake_order++ == index && ctx.gpr[2] == 1u);
    const auto address = out + index * 64u;
    if (expected_spacing != 0u) assert(rt.memory().load32(address) - last_stamp == expected_spacing);
    else assert(rt.memory().load32(address) == (index + 2u) * 1001000ull / 60u);
    last_stamp = rt.memory().load32(address);
    assert(rt.memory().load32(address + 4u) == (index == 0u ? ~pspweb::pad::kCross : pspweb::pad::kCross));
    assert(rt.memory().load32(address + 16u) == 0xDEADBEEFu); // only one record written
    ctx.gpr[4] = 0u;
    rt.invoke_import("ThreadManForUser", 0xAA73C935u, ctx);
}
void after_zero(Runtime &rt, Ctx &ctx) { finished(rt, ctx, 0u); }
void after_one(Runtime &rt, Ctx &ctx) { finished(rt, ctx, 1u); }
void after_two(Runtime &rt, Ctx &ctx) { finished(rt, ctx, 2u); }
void readers(Runtime &rt, Ctx &ctx) {
    // Drain the initial record and all samples that preceded entry.
    assert(call(rt, read, out + 1024u, 64u) > 0u);
    assert(call(rt, peek, out + 1024u, 1u) == 1u);
    last_stamp = rt.memory().load32(out + 1024u);
    rt.memory().store32(out + 2048u, 0x74736574u);
    rt.memory().store8(out + 2052u, 0u);
    for (unsigned i = 1u; i <= 2u; ++i) {
        Ctx create{};
        create.gpr[4] = out + 2048u;
        create.gpr[5] = 0x08804000u + i * 4u;
        create.gpr[6] = 0x30u; create.gpr[7] = 0x4000u;
        rt.invoke_import("ThreadManForUser", 0x446D8DE6u, create);
        assert(static_cast<std::int32_t>(create.gpr[2]) > 0);
        Ctx start{}; start.gpr[4] = create.gpr[2];
        rt.invoke_import("ThreadManForUser", 0xF475845Du, start);
    }
    park(rt, ctx, 0u);
}
void scheduled_readers(std::uint32_t cycle) {
    Runtime rt; pspweb::Kernel kernel(rt); kernel.install({});
    wake_order = 0u; expected_spacing = cycle;
    if (cycle != 0u) assert(call(rt, set_cycle, cycle) == 0u);
    kernel.set_pad(pspweb::pad::kCross);
    for (unsigned i = 0u; i < 3u; ++i) rt.memory().store32(out + i * 64u + 16u, 0xDEADBEEFu);
    rt.register_function(0x08804000u, readers, "readers");
    rt.register_function(0x08804004u, waiter_one, "waiter_one");
    rt.register_function(0x08804008u, waiter_two, "waiter_two");
    rt.register_function(0x08804100u, after_zero, "after_zero");
    rt.register_function(0x08804104u, after_one, "after_one");
    rt.register_function(0x08804108u, after_two, "after_two");
    kernel.boot(0x08804000u, 0u, "test");
    for (unsigned i = 0u; i < 8u && wake_order < 3u; ++i) kernel.frame(10.0);
    assert(!kernel.halted() && wake_order == 3u);
}
bool interrupt_checked;
void interrupt_reader(Runtime &rt, Ctx &ctx) {
    assert(call(rt, read, out, 1u) == 0x80020064u);
    assert(call(rt, negative_read, out, 0u) == 0x80020064u);
    assert(call(rt, read, out, 65u) == 0x80000104u); // size takes precedence
    assert(call(rt, peek, out, 1u) == 1u);
    interrupt_checked = true;
    ctx.pc = pspweb::kThreadReturnAddress;
}
}

int main() {
    Runtime rt;
    pspweb::Kernel kernel(rt); kernel.install({});
    auto &m = rt.memory();
    m.store32(out, 0xDEADBEEFu);
    assert(call(rt, peek, out, 0u) == 0u && m.load32(out) == 0xDEADBEEFu);
    assert(call(rt, peek, 0u, 0u) == 0u);
    for (auto count : {65u, 0xFFFFFFFFu}) {
        assert(call(rt, peek, out, count) == 0x80000104u);
        assert(m.load32(out) == 0xDEADBEEFu);
    }
    assert(call(rt, peek, 0u, 1u) == 0u);
    assert(call(rt, get_mode, out) == 0u && m.load32(out) == 0u);
    kernel.set_pad(pspweb::pad::kSquare, 10u, 240u);
    advance(kernel);
    assert(call(rt, peek, out, 1u) == 1u);
    const auto stamp = m.load32(out);
    assert(stamp == 16683u);
    assert(m.load32(out + 4u) == pspweb::pad::kSquare);
    assert(m.load8(out + 8u) == 128u && m.load8(out + 9u) == 128u);
    for (unsigned i = 10u; i < 16u; ++i) assert(m.load8(out + i) == 0u);
    // Input is sampled, rather than fabricating a new record at each peek.
    kernel.set_pad(pspweb::pad::kCircle, 2u, 253u);
    assert(call(rt, peek, out, 1u) == 1u && m.load32(out) == stamp);
    assert(m.load32(out + 4u) == pspweb::pad::kSquare);
    assert(call(rt, set_mode, 1u) == 0u);
    assert(call(rt, set_mode, 2u) == 0x80000107u);
    assert(call(rt, get_mode, out) == 0u && m.load32(out) == 1u);
    advance(kernel);
    assert(call(rt, peek, out, 2u) == 2u);
    assert(m.load32(out) == stamp && m.load32(out + 4u) == pspweb::pad::kSquare);
    assert(m.load32(out + 16u) == 33366u);
    assert(m.load32(out + 20u) == pspweb::pad::kCircle);
    assert(m.load8(out + 24u) == 2u && m.load8(out + 25u) == 253u);
    assert(call(rt, set_mode, 0u) == 1u && call(rt, set_mode, 1u) == 0u);
    // Wrap the 64-sample history and verify chronological records and guards.
    for (unsigned i = 0; i < 72u; ++i) {
        kernel.set_pad(i, static_cast<std::uint8_t>(i), static_cast<std::uint8_t>(255u - i));
        advance(kernel);
    }
    m.store32(out + 64u * 16u, 0xBADC0FFEu);
    assert(call(rt, peek, out, 64u) == 64u);
    for (unsigned i = 0; i < 64u; ++i) {
        assert(m.load32(out + i * 16u + 4u) == i + 8u);
        assert(m.load8(out + i * 16u + 8u) == i + 8u);
        assert(m.load32(out + i * 16u) == (i + 11u) * 1001000ull / 60u);
    }
    assert(m.load32(out + 64u * 16u) == 0xBADC0FFEu);
    kernel.set_pad(0xFFFFFFFFu);
    advance(kernel);
    assert(call(rt, peek, out, 1u) == 1u && m.load32(out + 4u) == 0x00FFFFFFu);
    assert(call(rt, peek, 0x09FFFFF0u, 2u) == 1u); // skip an unmapped record
    // Valid timer periods produce distinct sample timestamps at that cadence.
    assert(call(rt, set_cycle, 5555u) == 0u);
    assert(call(rt, get_cycle, out) == 0u && m.load32(out) == 5555u);
    for (auto cycle : {1u, 5554u, 20001u, 0xFFFFFFFFu})
        assert(call(rt, set_cycle, cycle) == 0x800001FEu);
    for (unsigned i = 0; i < 8u; ++i) advance(kernel);
    assert(call(rt, peek, out, 16u) == 16u);
    for (unsigned i = 1; i < 16u; ++i)
        assert(m.load32(out + i * 16u) - m.load32(out + (i - 1u) * 16u) == 5555u);
    assert(call(rt, set_cycle, 20000u) == 5555u);
    assert(call(rt, set_cycle, 0u) == 20000u);
    assert(call(rt, get_cycle, out) == 0u && m.load32(out) == 0u);
    // Consuming reads return the newest available records; peeks do not drain them.
    {
        Runtime r; pspweb::Kernel k(r); k.install({}); auto &mem = r.memory();
        for (unsigned i = 1u; i <= 4u; ++i) { k.set_pad(i); advance(k); }
        assert(call(r, negative_peek, out, 2u) == 2u);
        assert(mem.load32(out + 4u) == ~3u && mem.load32(out + 20u) == ~4u);
        assert(call(r, read, out, 2u) == 2u);
        assert(mem.load32(out + 4u) == 3u && mem.load32(out + 20u) == 4u);
        k.set_pad(pspweb::pad::kCircle); advance(k);
        assert(call(r, negative_read, out, 8u) == 1u);
        assert(mem.load32(out + 4u) == ~pspweb::pad::kCircle);
        assert(call(r, peek, out, 1u) == 1u && mem.load32(out + 4u) == pspweb::pad::kCircle);
        for (unsigned i = 0u; i < 72u; ++i) { k.set_pad(i); advance(k); }
        assert(call(r, read, out, 64u) == 63u);
        assert(mem.load32(out + 4u) == 9u && mem.load32(out + 62u * 16u + 4u) == 71u);
        assert(call(r, negative_read, out, 65u) == 0x80000104u);
    }
    // Latches accumulate transitions and held/released states until consumed.
    {
        Runtime r; pspweb::Kernel k(r); k.install({}); auto &mem = r.memory();
        assert(call(r, peek_latch, out) == 0u && mem.load32(out + 12u) == 0xFFFFFFFFu);
        assert(call(r, read_latch, out) == 0u);
        k.set_pad(pspweb::pad::kCross); advance(k); advance(k);
        k.set_pad(pspweb::pad::kCircle); advance(k);
        assert(call(r, peek_latch, out) == 3u);
        assert(mem.load32(out) == (pspweb::pad::kCross | pspweb::pad::kCircle));
        assert(mem.load32(out + 4u) == pspweb::pad::kCross);
        assert(mem.load32(out + 8u) == (pspweb::pad::kCross | pspweb::pad::kCircle));
        assert(mem.load32(out + 12u) == 0xFFFFFFFFu);
        assert(call(r, peek_latch, out) == 3u && call(r, read_latch, out) == 3u);
        assert(call(r, peek_latch, out) == 0u);
        for (unsigned i = 0u; i < 16u; i += 4u) assert(mem.load32(out + i) == 0u);
        k.set_pad(pspweb::pad::kCircle); advance(k);
        assert(call(r, read_latch, out) == 1u && mem.load32(out) == 0u);
        assert(mem.load32(out + 8u) == pspweb::pad::kCircle && mem.load32(out + 12u) == ~pspweb::pad::kCircle);
        mem.store32(0x09FFFFFCu, 0xBADC0FFEu);
        advance(k); assert(call(r, read_latch, 0x09FFFFFCu) == 1u);
        assert(mem.load32(0x09FFFFFCu) == 0xBADC0FFEu && call(r, peek_latch, out) == 0u);
    }
    scheduled_readers(0u);
    scheduled_readers(5555u);
    {
        Runtime r; pspweb::Kernel k(r); k.install({});
        r.register_function(0x08805000u, interrupt_reader, "interrupt_reader");
        Ctx reg{}; reg.gpr[4] = 30u; reg.gpr[5] = 0u; reg.gpr[6] = 0x08805000u;
        r.invoke_import("InterruptManager", 0xCA04A2B9u, reg);
        assert(reg.gpr[2] == 0u);
        Ctx enable{}; enable.gpr[4] = 30u; enable.gpr[5] = 0u;
        r.invoke_import("InterruptManager", 0xFB8E22ECu, enable);
        assert(enable.gpr[2] == 0u);
        k.frame(10.0);
        assert(interrupt_checked && !k.halted());
    }
    std::cout << "Controller: history, modes, consuming/negative reads, FIFO timed wakeups and button latches passed\n";
}
