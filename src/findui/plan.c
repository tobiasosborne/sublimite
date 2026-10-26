#include "findui/private.h"

static bool transfer(const os_file *file, void *bytes, size_t size, uint64_t offset, bool writing)
{
    if (offset > INT64_MAX || size > (uint64_t)INT64_MAX - offset) return false;
    uint8_t *p = bytes;
    while (size) {
        size_t n;
        os_err rc = writing ? os_file_pwrite((os_file *)file, p, size, offset, &n)
                            : os_file_pread((os_file *)file, p, size, offset, &n);
        if (rc || !n) return false;
        size -= (size_t)n; p += (size_t)n; offset += (size_t)n;
    }
    return true;
}
bool findui_plan_reset(findui_plan *plan)
{
    plan->records = plan->total = 0; plan->buffered = 0;
    plan->run = (findui_plan_run){0}; plan->ready = false;
    return os_file_set_size(&plan->file, 0) == OS_OK;
}
static bool write_buffer(findui_plan *plan)
{
    if (plan->records > (uint64_t)INT64_MAX / sizeof(findui_plan_run)) return false;
    size_t size = plan->buffered * sizeof(findui_plan_run);
    if (!transfer(&plan->file, plan->buffer, size, plan->records * sizeof(findui_plan_run), true)) return false;
    plan->records += plan->buffered; plan->buffered = 0;
    return true;
}
static bool append_run(findui_plan *plan)
{
    if (!plan->run.count) return true;
    plan->buffer[plan->buffered++] = plan->run;
    plan->run.count = 0;
    return plan->buffered < FINDUI_PLAN_RUNS || write_buffer(plan);
}
static bool add_run(findui_plan *plan, findui_plan_run incoming)
{
    findui_plan_run *r = &plan->run;
    if (incoming.ordinal != plan->total || incoming.count > UINT64_MAX - plan->total || incoming.start > incoming.end) return false;
    if (r->count && incoming.end - incoming.start == r->end - r->start) {
        uint64_t last = r->start + (r->count - 1u) * r->stride;
        uint64_t stride = incoming.start >= last ? incoming.start - last : UINT64_MAX;
        if ((r->count == 1 || stride == r->stride) && (incoming.count == 1 || stride == incoming.stride)) {
            if (r->count == 1) r->stride = stride;
            r->count += incoming.count; plan->total += incoming.count; return true;
        }
    }
    if (!append_run(plan)) return false;
    *r = incoming; plan->total += incoming.count; return true;
}
bool findui_plan_add(findui_plan *plan, uint64_t ordinal, find_capture range)
{ return add_run(plan, (findui_plan_run){ordinal, 1, range.start, range.end, 0}); }
bool findui_plan_add_mask(findui_plan *plan, uint64_t mask, uint64_t base, uint64_t ordinal)
{
    if (!mask) return true;
    unsigned first = (unsigned)__builtin_ctzll(mask);
    uint64_t shifted = mask >> first;
    uint64_t stride = 0;
    if (!(shifted & (shifted + 1u))) stride = 1; /* contiguous low bits */
    /* Division by binary 11 produces alternating bits; a space-separated
     * single-byte word mask therefore has a fixed stride of two bytes. */
    if (mask == UINT64_MAX / 3u || mask == (UINT64_MAX / 3u) << 1) stride = 2;
    if (stride) return add_run(plan, (findui_plan_run){ordinal, (unsigned)__builtin_popcountll(mask),
                                                     base + first, base + first + 1u, stride});
    while (mask) {
        unsigned bit = (unsigned)__builtin_ctzll(mask);
        if (!findui_plan_add(plan, ordinal++, (find_capture){base + bit, base + bit + 1u})) return false;
        mask &= mask - 1u;
    }
    return true;
}
bool findui_plan_seal(findui_plan *plan)
{
    if (!append_run(plan) || !write_buffer(plan)) return false;
    plan->ready = true; return true;
}
bool findui_plan_read(const findui_plan *plan, uint64_t record, findui_plan_run *run)
{
    if (!plan->ready || record >= plan->records || record > (uint64_t)INT64_MAX / sizeof *run) return false;
    return transfer(&plan->file, run, sizeof *run, record * sizeof *run, false);
}
bool findui_plan_ordinal(const findui_plan *plan, uint64_t ordinal, findui_plan_run *run)
{
    if (ordinal >= plan->total) return false;
    uint64_t low = 0, high = plan->records;
    while (low < high) {
        uint64_t mid = low + (high - low) / 2u;
        if (!findui_plan_read(plan, mid, run)) return false;
        if (ordinal >= run->ordinal + run->count) low = mid + 1u; else high = mid;
    }
    return findui_plan_read(plan, low, run);
}
bool findui_plan_window(const findui_plan *plan, uint64_t byte, uint64_t *record)
{
    uint64_t low = 0, high = plan->records;
    while (low < high) {
        uint64_t mid = low + (high - low) / 2u; findui_plan_run run;
        if (!findui_plan_read(plan, mid, &run)) return false;
        uint64_t end = run.end + (run.count - 1u) * run.stride;
        if (end < byte || (end == byte && run.start != run.end)) low = mid + 1u; else high = mid;
    }
    *record = low; return true;
}
