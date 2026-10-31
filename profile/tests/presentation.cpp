#include "frame_batch.hpp"
#include "ge.hpp"
#include "ge_worker.hpp"
#include <cassert>
#include <iostream>
#include <vector>

namespace {
void committed_frame_order(bool threaded, unsigned commit_step) {
    psprecomp::GuestMemory memory;
    pspweb::Ge ge(memory);
    pspweb::GeWorker worker(ge);
    if (threaded) worker.start([] {});
    std::uint32_t surface = 0u;
    std::vector<std::uint32_t> shown;
    unsigned step = 0u;
    bool running = true;
    const auto batch = pspweb::advance_frame_batch(2u, running, true, [&] {
        const auto color = ++step;
        worker.post([&, color] { surface = color; });
        return step == commit_step ? 1u : 0u;
    }, [&] {
        worker.post_present([&] { shown.push_back(surface); return true; });
    });
    worker.call([] {}); // all queued drawing/presentation must have completed
    assert(batch.steps == 2u && batch.commits == 1u);
    assert(shown == std::vector<std::uint32_t>{commit_step});
    const auto stats = worker.take_presentation_stats();
    assert(stats.frames == 1u && stats.intervals == 0u && stats.queue_ms >= 0.0);
    worker.post_present([] { return false; });
    worker.call([] {});
    assert(worker.take_presentation_stats().frames == 0u);
    worker.post_present([] { return true; });
    worker.call([] {});
    const auto next = worker.take_presentation_stats();
    assert(next.frames == 1u && next.intervals == 1u && next.interval_ms >= 0.0);
    worker.reset_presentation_stats();
    worker.post_present([] { return true; });
    worker.call([] {});
    assert(worker.take_presentation_stats().intervals == 0u);
}
void cadence_and_halt() {
    unsigned steps = 0u, presents = 0u;
    bool running = true;
    auto step = [&] { ++steps; return 0u; };
    auto present = [&] { ++presents; };
    auto result = pspweb::advance_frame_batch(0u, running, true, step, present);
    assert(result.steps == 0u && steps == 0u && presents == 0u);
    result = pspweb::advance_frame_batch(2u, running, true, step, present);
    assert(result.steps == 2u && result.commits == 0u && presents == 0u);
    result = pspweb::advance_frame_batch(2u, running, false, step, present);
    assert(result.steps == 2u && presents == 2u);
    result = pspweb::advance_frame_batch(2u, running, true, [&] {
        running = false;
        return 1u;
    }, present);
    assert(result.steps == 1u && result.commits == 1u && presents == 3u);
    result = pspweb::advance_frame_batch(2u, running, true, step, present);
    assert(result.steps == 0u && presents == 3u);
}
}
int main() {
    for (bool threaded : {false, true}) {
        committed_frame_order(threaded, 1u);
        committed_frame_order(threaded, 2u);
    }
    cadence_and_halt();
    std::cout << "Presentation: committed surface ordering across catch-up steps, cadence and halt passed\n";
}
