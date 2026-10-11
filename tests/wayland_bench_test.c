/* (P) Ordered synthetic clock ticks derive from the minimum nonzero tick and
 * successive increments, not a latency limit. Frame/sequence identities derive
 * from the nonzero render/input identity contract; zero means no key. */
#define main wayland_bench_driver_main
#include "../bench/wayland_bench.c"
#undef main
#include <assert.h>
int main(void)
{
    uint64_t tick = 1; uint64_t key_sequence = 1;
    render_backend backend = {.submitted_ns = tick + 1};
    wayland_bench_stamp s = {.backend = &backend};
    ingress(&s, key_sequence, tick);
    ingress(&s, key_sequence + 1, tick + 1);
    assert(s.ingress == tick && s.sequence == key_sequence);
    editor_frame frame = {.id = 1, .first_sequence = key_sequence,
        .last_sequence = key_sequence + 1, .present_ns = backend.submitted_ns + 1};
    present(&s, &frame);
    assert(s.t4 == backend.submitted_ns && s.frame == frame.id);
    uint64_t captured = s.t4; uint32_t captured_frame = s.frame;
    backend.submitted_ns++;
    frame = (editor_frame){.id = captured_frame + 1, .present_ns = backend.submitted_ns};
    present(&s, &frame);
    assert(s.t4 == captured && s.frame == captured_frame);
    puts("wayland_bench_test: first matching frame captures backend T4; observer delay and later caret frames cannot replace it");
    return 0;
}
