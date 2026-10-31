// Partition allocation behavior observed through the public SysMem HLE.
#include "kernel.hpp"
#include <cassert>
#include <iostream>

namespace {
using psprecomp::Runtime;
constexpr auto sysmem = "SysMemUserForUser";
// Kernel::install already reserves the first 16 KiB for the loader.
constexpr std::uint32_t begin = pspweb::kUserPartitionStart + 0x4000u;
constexpr std::uint32_t end = 0x0A000000u;
std::uint32_t call(Runtime &rt, std::uint32_t nid, std::uint32_t a0 = 0,
                   std::uint32_t a1 = 0, std::uint32_t a2 = 0,
                   std::uint32_t a3 = 0, std::uint32_t t0 = 0) {
    psprecomp::AllegrexContext ctx{};
    ctx.gpr[4] = a0; ctx.gpr[5] = a1; ctx.gpr[6] = a2;
    ctx.gpr[7] = a3; ctx.gpr[8] = t0;
    rt.invoke_import(sysmem, nid, ctx);
    return ctx.gpr[2];
}
std::uint32_t alloc(Runtime &rt, std::uint32_t type, std::uint32_t size,
                    std::uint32_t address = 0) {
    return call(rt, 0x237DBD4Fu, 2, 0, type, size, address);
}
std::uint32_t head(Runtime &rt, std::uint32_t uid) {
    assert(static_cast<std::int32_t>(uid) > 0);
    return call(rt, 0x9D9A5BA1u, uid);
}
void release(Runtime &rt, std::uint32_t uid) {
    assert(call(rt, 0xB6D61D02u, uid) == 0);
}
void ncaa_heap_alignment() {
    Runtime rt;
    pspweb::Kernel kernel(rt); kernel.install({});
    // NCAA's loaded image ends at ...8E80 and occupies 256-byte pages.
    kernel.reserve(begin, 0x006C4F00u, "synthetic image");
    const auto free_before = call(rt, 0xF919F628u);
    const auto heap = alloc(rt, 3, 0x01036400u, 4096);
    assert(head(rt, heap) == 0x08EC9000u); // independently observed in PPSSPP
    assert(call(rt, 0xF919F628u) == free_before - 0x01036400u);
    // Alignment padding stays usable; it isn't silently charged to the heap.
    const auto padding = alloc(rt, 0, 1);
    assert(head(rt, padding) == 0x08EC8F00u);
    release(rt, heap); release(rt, padding);
    assert(call(rt, 0xF919F628u) == free_before);
    assert(call(rt, 0xA291F107u) == free_before);
}
void high_and_fragmented_alignment() {
    Runtime rt;
    pspweb::Kernel kernel(rt); kernel.install({});
    kernel.reserve(begin, 0x100, "low guard");
    kernel.reserve(end - 0x1800, 0x1800, "high guard");
    const auto high = alloc(rt, 4, 0x301, 4096);
    assert(head(rt, high) == end - 0x2000);
    // A gap can be large enough in bytes but too small after alignment.
    kernel.reserve(begin + 0x1000, 0x1000, "middle guard");
    const auto low = alloc(rt, 3, 0x100, 4096);
    assert(head(rt, low) == begin + 0x2000);
    const auto small_alignment = alloc(rt, 3, 1, 16);
    assert(head(rt, small_alignment) == begin + 0x100);
    release(rt, high); release(rt, low); release(rt, small_alignment);
}
void invalid_and_fixed_requests() {
    Runtime rt;
    pspweb::Kernel kernel(rt); kernel.install({});
    kernel.reserve(begin, 0x100, "guard");
    const auto free_before = call(rt, 0xF919F628u);
    assert(alloc(rt, 5, 1) == 0x800200D8u);
    assert(alloc(rt, 3, 1, 0) == 0x800200E4u);
    assert(alloc(rt, 4, 1, 3072) == 0x800200E4u);
    assert(alloc(rt, 0, 0) == 0x800200D9u);
    assert(alloc(rt, 3, 0xFFFFFFFFu, 4096) == 0x800200D9u);
    assert(alloc(rt, 3, 256, 0x80000000u) == 0x800200D9u);
    assert(call(rt, 0xF919F628u) == free_before);
    const auto fixed = alloc(rt, 2, 1, begin + 0x1FF);
    assert(head(rt, fixed) == begin + 0x100);
    assert(alloc(rt, 2, 1, begin + 0x100) == 0x800200D9u);
    release(rt, fixed);
    assert(call(rt, 0xF919F628u) == free_before);
}
void partition_capacity(std::uint32_t ram_mb) {
    Runtime rt(ram_mb << 20u);
    pspweb::Kernel kernel(rt); kernel.install({});
    const auto limit = psprecomp::GuestMemory::kPhysicalBase + rt.memory().size();
    const auto capacity = limit - begin;
    assert(call(rt, 0xF919F628u) == capacity);
    assert(call(rt, 0xA291F107u) == capacity);
    const auto high = alloc(rt, 4, 0x1000, 4096);
    assert(head(rt, high) == limit - 0x1000);
    // Generated units cache this view, including its width-specific limits.
    const auto view = rt.memory().aot_fast_view();
    view.aot_store32(limit - 4u, 0x1234ABCDu);
    assert(view.aot_load32((limit - 4u) | 0x40000000u) == 0x1234ABCDu);
    view.aot_store16((limit - 2u) | 0x40000000u, 0x5678u);
    assert(view.aot_load16(limit - 2u) == 0x5678u);
    view.aot_store8(limit - 1u, 0x9Au);
    assert(view.aot_load8((limit - 1u) | 0x40000000u) == 0x9Au);
    assert(rt.memory().load32(limit - 4u) == 0x9A78ABCDu);
    assert(!rt.memory().contains(limit, 1));
    assert(alloc(rt, 2, 0x100, limit) == 0x800200D9u);
    const auto rest = alloc(rt, 0, capacity - 0x1000);
    assert(head(rt, rest) == begin);
    assert(call(rt, 0xF919F628u) == 0u && call(rt, 0xA291F107u) == 0u);
    assert(alloc(rt, 0, 1) == 0x800200D9u);
    release(rt, high); release(rt, rest);
    assert(call(rt, 0xF919F628u) == capacity && call(rt, 0xA291F107u) == capacity);
    // Use the public import so a missing RAM-size argument in the host path
    // cannot silently pass a test that only exercises DisplayState directly.
    auto display = [&](std::uint32_t address) {
        psprecomp::AllegrexContext ctx{};
        ctx.gpr[4] = address; ctx.gpr[5] = 512u;
        ctx.gpr[6] = 3u; ctx.gpr[7] = 1u;
        rt.invoke_import("sceDisplay", 0x289D82FEu, ctx);
        return ctx.gpr[2];
    };
    assert(display(limit - 0x1000) == 0u);
    assert(display((limit - 0x1000) | 0x40000000u) == 0u);
    assert(display(limit) == 0x80000103u);
    assert(display(0x0A000000u) == (ram_mb == 64u ? 0u : 0x80000103u));
}
}
int main() {
    ncaa_heap_alignment();
    high_and_fragmented_alignment();
    invalid_and_fixed_requests();
    partition_capacity(32u);
    partition_capacity(64u);
    std::cout << "Memory: NCAA heap alignment, placement, padding, 32/64 MiB capacity, AOT upper-memory access, display bounds and exhaustion/release passed\n";
}
