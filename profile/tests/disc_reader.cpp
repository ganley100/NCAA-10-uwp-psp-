// Reproduce the real generated reader's unchecked NULL destination without
// loading an ISO or making any disc read. Its caller has already allocated NULL.
#include "ncaa_disc_trace.hpp"
#include "ncaa_pointer_trace.hpp"
#include "psprecomp/instruction_trace.hpp"
#include "../generated/ncaa10/generated_units.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>

namespace {
pspweb::NcaaPointerTrace provenance;
std::ostringstream provenance_log;
struct LocalBoundary {};
std::uint32_t local_boundary{};
void observe_provenance(const psprecomp::Runtime &rt, const psprecomp::AllegrexContext &ctx,
                        std::uint32_t pc, std::uint32_t word, bool delay) {
    if (pc == local_boundary) throw LocalBoundary{};
    provenance.observe(rt, ctx, pc, word, delay, 0u, provenance_log);
}
void allocation_capacity_rejection() {
    psprecomp::Runtime rt;
    psprecomp::AllegrexContext ctx{};
    ctx.pc = 0x08CD3080u; // real comparison after the heap-capacity query
    ctx.gpr[2] = 0x391C0u;
    ctx.gpr[17] = 0x39480u;
    ctx.gpr[21] = 0x08900000u;
    ctx.gpr[29] = 0x09FFF000u;
    rt.memory().store32(ctx.gpr[21] + 16u, 0u); // no failure callback
    rt.memory().store32(ctx.gpr[29] + 512u, 0x08000000u); // synthetic caller
    psprecomp::recomp_unit_0307(rt, ctx);
    assert(ctx.gpr[2] == 0u && ctx.pc == 0x08000000u);
    assert(ctx.gpr[29] == 0x09FFF210u);
    std::cout << "PASS: real generated capacity comparison rejects 0x391C0 < 0x39480 and explicitly returns zero\n";
}
void allocation_return_propagation(bool full_provenance) {
    // Execute the real caller with an observed allocation failure. Stop at
    // external call boundaries; supply only synthetic descriptor addresses,
    // without pretending that any allocation or disc read succeeded.
    psprecomp::Runtime rt;
    rt.register_generated_unit(0u, 0x08804000u, 0x4000u,
        [](psprecomp::Runtime &, psprecomp::AllegrexContext &) {});
    for (auto pc : {0x08CCC5F8u, 0x08CCB764u, 0x08CE250Cu, 0x08CC848Cu,
                    0x08CE25A0u, 0x08CCB6D0u, 0x08C745D4u, 0x08C73C4Cu,
                    0x08C73CB0u, 0x08C79470u, 0x08C7A138u, 0x08C7A590u,
                    0x08C7BCF4u})
        rt.register_function(pc, [](psprecomp::Runtime &, psprecomp::AllegrexContext &) {
            assert(false && "External boundary must return to test driver");
        }, "synthetic_external_boundary");
    auto &mem = rt.memory();
    psprecomp::reset_aot_instruction_trace();
    psprecomp::set_aot_instruction_observer(full_provenance ? observe_provenance : nullptr);
    psprecomp::AllegrexContext ctx{};
    constexpr auto owner = 0x08900000u, request = owner + 0x100u, stream = owner + 0x200u;
    constexpr auto descriptor = owner + 0x400u, file = owner + 0x500u;
    for (auto address : {owner + 28u, request + 4u, stream + 44u, descriptor + 32u})
        mem.store32(address, 0xDEADBEEFu); // stores must change pre-existing values
    ctx.pc = 0x08CDB4A4u;
    ctx.gpr[2] = 0u;
    ctx.gpr[30] = owner;
    ctx.gpr[29] = 0x09FFF000u;
    ctx.gpr[16] = 0x140067Au;
    mem.store32(ctx.gpr[29] + 8u, 0x39480u);
    mem.store32(ctx.gpr[29] + 152u, 0xA6BA80u);
    psprecomp::recomp_unit_0309(rt, ctx);
    assert(ctx.pc == 0x08CCC5F8u && ctx.gpr[5] == 0u && ctx.gpr[9] == owner);
    assert(mem.load32(owner + 28u) == 0u && ctx.gpr[6] == 0x39480u);
    psprecomp::recomp_unit_0306(rt, ctx);
    assert(ctx.pc == 0x08CCB764u && ctx.gpr[22] == 0u);
    ctx.pc = 0x08CCC63Cu; ctx.gpr[2] = request; // synthetic stream-request object
    psprecomp::recomp_unit_0306(rt, ctx);
    assert(mem.load32(request + 4u) == 0u && mem.load32(request + 28u) == owner);
    assert(mem.load32(request + 8u) == 0x39480u);
    // Resume after the two locking helpers and file-index helper, which are
    // outside this pointer-propagation test.
    ctx.pc = 0x08CCC678u; psprecomp::recomp_unit_0306(rt, ctx);
    ctx.pc = 0x08CCC68Cu; psprecomp::recomp_unit_0306(rt, ctx);
    ctx.pc = 0x08CCC694u; psprecomp::recomp_unit_0306(rt, ctx);
    ctx.pc = 0x08CCC69Cu; ctx.gpr[2] = 0u;
    mem.store32(0x08E3619Cu, owner + 0x300u);
    psprecomp::recomp_unit_0306(rt, ctx);
    assert(ctx.pc == 0x08C745D4u && ctx.gpr[6] == 0u && ctx.gpr[7] == 0x39480u);
    psprecomp::recomp_unit_0284(rt, ctx);
    assert(ctx.pc == 0x08C73C4Cu && ctx.gpr[17] == 0u);
    ctx.pc = 0x08C7460Cu; ctx.gpr[2] = stream; // synthetic file-stream object
    psprecomp::recomp_unit_0284(rt, ctx);
    assert(ctx.pc == 0x08C73CB0u);
    ctx.pc = 0x08C74620u;
    mem.store32(0x08E30F80u, 0x20000u);
    psprecomp::recomp_unit_0284(rt, ctx);
    assert(mem.load32(stream + 44u) == 0u && mem.load32(stream + 40u) == 0x39480u);
    assert(ctx.pc == 0x08C79470u && ctx.gpr[5] == 0u && ctx.gpr[7] == 0x20000u);
    if (full_provenance) {
    // Real queue construction, with a synthetic file and descriptor supplied
    // at external allocation boundaries. No disc data is read.
    ctx.gpr[4] = file; ctx.gpr[5] = 0xA6BA80u;
    const auto queue_until = [&](std::uint32_t pc) {
        // Same-unit generated calls use labels, so stop before the external
        // fixture boundary through the read-only instruction observer.
        local_boundary = pc;
        try { psprecomp::recomp_unit_0285(rt, ctx); }
        catch (const LocalBoundary &) { ctx.pc = pc; }
        local_boundary = 0u;
    };
    queue_until(0x08C7A138u);
    assert(ctx.pc == 0x08C7A138u);
    ctx.pc = 0x08C794B0u; ctx.gpr[2] = descriptor;
    mem.store32(descriptor, 0x0100067Au);
    queue_until(0x08C7A590u);
    assert(ctx.pc == 0x08C7A590u && mem.load32(descriptor + 32u) == 0u);
    assert(mem.load32(descriptor + 20u) == stream);
    // Correlate the public submission's validated caller frame to the queued
    // descriptor. This fixture does not execute the OS I/O wrappers.
    ctx = {}; ctx.pc = 0x08C7F660u; ctx.gpr[29] = 0x09FFEF00u;
    ctx.gpr[4] = 139u; ctx.gpr[6] = 0xA6BA80u; ctx.gpr[7] = 0x20000u;
    mem.store32(ctx.gpr[29] + 100u, 0x08C785F0u);
    mem.store32(ctx.gpr[29] + 148u, descriptor);
    mem.store32(ctx.gpr[29] + 180u, 0x08C79FA8u);
    observe_provenance(rt, ctx, ctx.pc, 0x27BDFFE0u, false);
    // Execute the actual reader prologue, including the stack-slot store.
    ctx.pc = 0x08C7ECFCu; ctx.gpr[6] = 0x20000u; ctx.gpr[7] = 0xA6BA80u;
    psprecomp::recomp_unit_0286(rt, ctx);
    assert(ctx.pc == 0x08C7BCF4u);
    ctx.pc = 0x08C7EDA4u; ctx.gpr[2] = 0x20u;
    ctx.gpr[30] = ctx.gpr[23] = 0x08E30000u;
    mem.store32(ctx.gpr[29] + 44u, 0u);
    mem.store8(0x08E31128u, 0u); mem.store32(0x08E3112Cu, 0xFD4AC67Au);
    bool fault = false;
    try { psprecomp::recomp_unit_0286(rt, ctx); }
    catch (const std::exception &) { fault = true; }
    assert(fault && !mem.contains(0u, 4u));
    {
        const auto log = provenance_log.str();
        for (const auto &event : {
                "id=1 event=allocator-return", "id=1 event=descriptor", "id=1 event=reader-submit",
                "id=1 event=reader-entry", "id=1 event=read pc=0x08C7EDAC",
                "id=1 event=write-completed pc=0x08CDB4A4",
                "id=1 event=write-completed pc=0x08CCC644",
                "id=1 event=write-completed pc=0x08C74640",
                "id=1 event=write-completed pc=0x08C79518",
                "id=1 event=write-completed pc=0x08C7ED04", "result=FAULT-no-write"})
            assert(log.find(event) != std::string::npos);
        assert(log.find("write-completed pc=0x08C7EE50") == std::string::npos);
        std::cout << log;
    }
    }
    psprecomp::set_aot_instruction_observer(nullptr);
    std::cout << "PASS: actual generated allocation return -> owner+28 -> stream-request+4 -> file-stream+44 -> queued buffer, no pointer loss\n";
}
struct Result { std::string error; psprecomp::AllegrexContext ctx; };
Result run(bool hook, unsigned prefix_order) {
    psprecomp::reset_aot_instruction_trace();
    psprecomp::Runtime rt;
    auto &mem = rt.memory();
    psprecomp::AllegrexContext ctx{};
    ctx.pc = ctx.gpr[31] = 0x08C7EDA4u;
    ctx.gpr[2] = 0x20u; // UMD ready, takes BNEL and its LW delay slot.
    ctx.gpr[4] = 0x12345678u; // deliberately stale entry argument; overwritten.
    ctx.gpr[5] = 0x11111111u; // proves the later move, rather than an entry NULL.
    ctx.gpr[29] = 0x09FFF000u;
    ctx.gpr[30] = ctx.gpr[23] = 0x08E30000u;
    ctx.gpr[20] = 0x08E3112Cu;
    ctx.gpr[19] = 0x08E3113Cu;
    mem.store8(0x08E31128u, static_cast<std::uint8_t>(prefix_order));
    mem.store32(0x08E3112Cu, 0xFD4AC67Au);
    mem.store32(0x08E3113Cu, 0xCAFEBABEu);
    mem.store32(ctx.gpr[29] + 36u, 0u); // allocation failure's destination
    mem.store32(ctx.gpr[29] + 44u, 0u); // copy the sector prefix
    mem.store32(ctx.gpr[29] + 52u, 0x20000u);
    mem.store32(ctx.gpr[29] + 48u, 0xA6BA80u);
    mem.store32(ctx.gpr[29] + 104u, 0x08CCC724u);
    if (hook) {
        const auto before = ctx;
        std::array<std::uint8_t, 320> stack_before{}, stack_after{};
        mem.copy_out(ctx.gpr[29], stack_before);
        std::ostringstream log;
        pspweb::trace_ncaa_disc_reader(mem, ctx, log);
        mem.copy_out(ctx.gpr[29], stack_after);
        assert(std::memcmp(&before, &ctx, sizeof(ctx)) == 0);
        assert(stack_before == stack_after);
        assert(log.str().find("buffer=0x0") != std::string::npos);
    }
    try { psprecomp::recomp_unit_0286(rt, ctx); }
    catch (const std::exception &e) {
        assert(ctx.gpr[5] == 0u && ctx.gpr[17] == 0u);
        assert(ctx.gpr[4] == (prefix_order ? 0xCAFEBABEu : 0xFD4AC67Au));
        assert(!mem.contains(0u, 1u));
        return {e.what(), ctx};
    }
    assert(false && "Generated reader must reject the NULL store");
    return {};
}
}
int main(int argc, char **) {
    const bool diagnostic = argc > 1;
    allocation_capacity_rejection();
    allocation_return_propagation(argc > 2);
    for (unsigned order : {0u, 1u}) {
        const auto plain = run(false, order), observed = run(true, order);
        assert(plain.error == observed.error);
        assert(std::memcmp(&plain.ctx, &observed.ctx, sizeof(plain.ctx)) == 0);
        if (diagnostic) {
            assert(plain.error.find(order ? "instruction_pc=0x08C7EE90" : "instruction_pc=0x08C7EE50") != std::string::npos);
            assert(plain.error.find("instruction_word=0xACA40000") != std::string::npos);
            assert(plain.error.find("operation=STORE access_width=4 base_register=r5 base_value=0x00000000 signed_offset=0 effective_address=0x00000000") != std::string::npos);
            assert(plain.error.find("in_delay_slot=0") != std::string::npos);
            assert(plain.error.find("0x08C7EDAC") != std::string::npos); // executed likely delay slot
            assert(plain.error.find("previous_branch_pc=0x08C7EE44") != std::string::npos);
            assert(plain.error.find("r5:0x11111111->0x00000000") != std::string::npos);
            std::cout << plain.error << '\n';
        } else {
            assert(plain.error.find("access outside") != std::string::npos);
        }
    }
    std::cout << "PASS: real generated disc-reader NULL store, both prefix orders, hook transparency, precise pre-access fault\n";
}
