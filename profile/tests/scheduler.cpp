// Timed priority preemption while the current guest thread makes no HLE calls.
#include "kernel.hpp"
#include <cassert>
#include <chrono>
#include <iostream>

namespace {
using psprecomp::Runtime;
using Ctx = psprecomp::AllegrexContext;
using Clock = std::chrono::steady_clock;
constexpr auto tm = "ThreadManForUser";
constexpr std::uint32_t base = 0x08804000u;
pspweb::Kernel *kernel;
std::uint32_t child_priority, parent_sp, wake_frame;
bool woke, parked, completed, woke_before_park;
Clock::time_point busy_until;

void exit(Runtime &rt, Ctx &ctx) {
    ctx.gpr[4] = 0u;
    rt.invoke_import(tm, 0xAA73C935u, ctx);
}
void child(Runtime &rt, Ctx &ctx) {
    ctx.gpr[8] = 0x12345678u;
    ctx.gpr[4] = 2000u;
    ctx.gpr[31] = base + 24u;
    rt.invoke_import(tm, 0xCEADEB47u, ctx);
}
void child_woke(Runtime &rt, Ctx &ctx) {
    assert(ctx.gpr[2] == 0u && ctx.gpr[8] == 0x12345678u);
    woke = true;
    woke_before_park = !parked;
    wake_frame = kernel->display().vcount;
    exit(rt, ctx);
}
void after_park(Runtime &rt, Ctx &ctx) {
    assert(woke && ctx.gpr[8] == 0xCAFEBABEu && ctx.gpr[29] == parent_sp);
    completed = true;
    exit(rt, ctx);
}
void busy(Runtime &rt, Ctx &ctx) {
    assert(ctx.gpr[8] == 0xCAFEBABEu && ctx.gpr[29] == parent_sp);
    if (woke) {
        assert(ctx.gpr[2] == 0xDEADBEEFu); // restore the preempted return register
        completed = true;
        exit(rt, ctx);
    } else if (Clock::now() >= busy_until) {
        parked = true;
        ctx.gpr[4] = 1000u;
        ctx.gpr[31] = base + 28u;
        rt.invoke_import(tm, 0xCEADEB47u, ctx);
    } else {
        // Give the timer time to expire without entering the guest kernel.
        const auto until = Clock::now() + std::chrono::microseconds(2);
        while (Clock::now() < until) {}
        ctx.pc = ctx.pc == base + 16u ? base + 32u : base + 16u;
    }
}
void begin_busy(Runtime &, Ctx &ctx) {
    assert(ctx.gpr[2] == 0u);
    parent_sp = ctx.gpr[29];
    ctx.gpr[2] = 0xDEADBEEFu;
    ctx.gpr[8] = 0xCAFEBABEu;
    busy_until = Clock::now() + std::chrono::milliseconds(12);
    ctx.pc = base + 16u;
}
void after_start(Runtime &rt, Ctx &ctx) {
    assert(ctx.gpr[2] == 0u);
    ctx.gpr[4] = 0u;
    ctx.gpr[5] = 0x30u;
    ctx.gpr[31] = base + 12u;
    rt.invoke_import(tm, 0x71BC9871u, ctx);
}
void after_create(Runtime &rt, Ctx &ctx) {
    assert(static_cast<std::int32_t>(ctx.gpr[2]) > 0);
    ctx.gpr[4] = ctx.gpr[2];
    ctx.gpr[5] = ctx.gpr[6] = 0u;
    ctx.gpr[31] = base + 8u;
    rt.invoke_import(tm, 0xF475845Du, ctx);
}
void setup(Runtime &rt, Ctx &ctx) {
    ctx.gpr[4] = 0u;
    ctx.gpr[5] = base + 20u;
    ctx.gpr[6] = child_priority;
    ctx.gpr[7] = 0x4000u;
    ctx.gpr[8] = 0u;
    ctx.gpr[31] = base + 4u;
    rt.invoke_import(tm, 0x446D8DE6u, ctx);
}
void timed_priority(std::uint32_t priority) {
    Runtime rt;
    pspweb::Kernel k(rt); kernel = &k; k.install({});
    child_priority = priority;
    woke = parked = completed = woke_before_park = false;
    wake_frame = 0u;
    rt.register_function(base, setup, "setup");
    rt.register_function(base + 4u, after_create, "after_create");
    rt.register_function(base + 8u, after_start, "after_start");
    rt.register_function(base + 12u, begin_busy, "begin_busy");
    rt.register_function(base + 16u, busy, "busy_a");
    rt.register_function(base + 20u, child, "child");
    rt.register_function(base + 24u, child_woke, "child_woke");
    rt.register_function(base + 28u, after_park, "after_park");
    rt.register_function(base + 32u, busy, "busy_b");
    k.boot(base, 0u, "test", 0x60u);
    (void)k.frame(50.0);
    assert(!k.halted() && woke && completed && wake_frame == 1u);
    assert(woke_before_park == (priority < 0x30u));
}
}
int main() {
    timed_priority(0x10u);
    timed_priority(0x30u);
    timed_priority(0x40u);
    std::cout << "Scheduler: timed high-priority preemption, equal/lower priority deferral and register restoration passed\n";
}
