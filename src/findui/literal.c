/* Persistent ASCII-folding KMP. Yield at metered byte boundaries; poll inside
 * preprocessing/failure chains too. Accepted matches reset KMP, rejected word
 * matches retain overlap state. */
#include "findui/private.h"
#include "file/file.h"
#include <emmintrin.h>

static uint8_t fold(uint8_t byte, bool sensitive)
{
    return !sensitive && byte >= 'A' && byte <= 'Z' ? (uint8_t)(byte + ('a' - 'A')) : byte;
}
static bool word(uint8_t byte)
{
    return (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
           (byte >= '0' && byte <= '9') || byte == '_';
}
static bool poll_units(findui_slot *slot, work_ctx *context, size_t units)
{
    /* Preserve the original folding visitor's conservative polling factor:
     * a byte includes comparison, boundary/ring work and an advance. */
    return units % (FIND_POLL_UNITS / 4u) == 0 &&
        (work_should_stop(context) || file_snapshot_faulted(slot->snapshot));
}
static __m128i between(__m128i bytes, char low, char high)
{
    return _mm_and_si128(_mm_cmpgt_epi8(bytes, _mm_set1_epi8((char)(low - 1))),
                         _mm_cmpgt_epi8(_mm_set1_epi8((char)(high + 1)), bytes));
}
static uint64_t word_mask(const uint8_t *bytes)
{
    uint64_t mask = 0;
    for (unsigned i = 0; i < 4; i++) {
        __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(bytes + i * 16));
        __m128i w = _mm_or_si128(between(_mm_or_si128(b, _mm_set1_epi8(32)), 'a', 'z'),
            _mm_or_si128(between(b, '0', '9'), _mm_cmpeq_epi8(b, _mm_set1_epi8('_'))));
        mask |= (uint64_t)(unsigned)_mm_movemask_epi8(w) << (i * 16);
    }
    return mask;
}
static find_code byte_step(findui_slot *slot, work_ctx *context, find_visit *visit)
{
    uint64_t total = piece_snapshot_len(slot->snapshot);
    uint8_t needle = slot->query[0], previous = 0;
    bool folded = !slot->options.match_case && fold(needle, false) >= 'a' && fold(needle, false) <= 'z';
    __m128i low = _mm_set1_epi8((char)needle);
    uint8_t lower = fold(needle, false);
    uint8_t other = folded ? (needle == lower ? (uint8_t)(lower - ('a' - 'A')) : lower) : needle;
    __m128i high = _mm_set1_epi8((char)other);
    if (slot->offset && slot->options.whole_word && piece_snapshot_read(slot->snapshot, slot->offset - 1u, &previous, 1)) return FIND_ERR_ARGUMENT;
    piece_iter it; const uint8_t *bytes; size_t length, blocks = 0;
    piece_iter_begin_snapshot(&it, slot->snapshot, slot->offset);
    while (piece_iter_next(&it, &bytes, &length)) {
        size_t i = 0;
        /* Four SSE2 vectors of sixteen bytes form the existing 64-byte mask.
         * A worker quantum handles at most FIND_POLL_UNITS masks (=64 KiB),
         * checking cancellation before every mask, not merely every slice. */
        while (length - i >= 64) {
            if (blocks++ == FIND_POLL_UNITS) return FIND_OK;
            if (work_should_stop(context) || file_snapshot_faulted(slot->snapshot)) return FIND_CANCELLED;
            uint64_t mask = 0;
            for (unsigned j = 0; j < 4; j++) {
                __m128i b = _mm_loadu_si128((const __m128i *)(const void *)(bytes + i + j * 16));
                __m128i equal = _mm_cmpeq_epi8(b, low);
                if (folded) equal = _mm_or_si128(equal, _mm_cmpeq_epi8(b, high));
                mask |= (uint64_t)(unsigned)_mm_movemask_epi8(equal) << (j * 16);
            }
            if (slot->options.whole_word) {
                uint8_t next = 0;
                if (i + 64 < length) next = bytes[i + 64];
                else if (slot->offset + 64 < total && piece_snapshot_read(slot->snapshot, slot->offset + 64, &next, 1)) return FIND_ERR_ARGUMENT;
                uint64_t words = word_mask(bytes + i);
                mask &= ~((words << 1) | (word(previous) ? 1u : 0u));
                mask &= ~((words >> 1) | (word(next) ? UINT64_C(1) << 63 : 0));
            }
            if (!find_visit_mask(visit, mask, slot->offset, visit->total)) return FIND_CANCELLED;
            visit->total += (unsigned)__builtin_popcountll(mask);
            previous = bytes[i + 63]; i += 64; slot->offset += 64;
        }
        for (; i < length; i++) {
            if (blocks++ == FIND_POLL_UNITS) return FIND_OK;
            if (work_should_stop(context) || file_snapshot_faulted(slot->snapshot)) return FIND_CANCELLED;
            uint8_t next = 0;
            if (i + 1u < length) next = bytes[i + 1u];
            else if (slot->offset + 1u < total && piece_snapshot_read(slot->snapshot, slot->offset + 1u, &next, 1)) return FIND_ERR_ARGUMENT;
            if (fold(bytes[i], slot->options.match_case) == fold(needle, slot->options.match_case) &&
                (!slot->options.whole_word || (!word(previous) && !word(next)))) {
                if (!find_visit_add(visit, visit->total, (find_capture){slot->offset, slot->offset + 1u})) return FIND_CANCELLED;
                visit->total++;
            }
            previous = bytes[i]; slot->offset++;
        }
    }
    slot->scan_done = true; return FIND_OK;
}
typedef struct window_output {
    find_visit *visit;
    uint64_t base, end, next;
} window_output;
static bool window_range(void *user, uint64_t ordinal, find_capture range)
{
    window_output *out = user; (void)ordinal;
    range.start += out->base; range.end += out->base;
    if (range.start >= out->end) return true;
    if (!find_visit_add(out->visit, out->visit->total, range)) return false;
    out->visit->total++;
    if (range.end > out->next) out->next = range.end;
    return true;
}
static find_code sensitive_step(findui_slot *slot, work_ctx *context, find_visit *visit)
{
    uint64_t total = piece_snapshot_len(slot->snapshot), remaining = total - slot->offset;
    /* Reuse the byte-mask quantum. Bound dense matches by the core's offset
     * capacity as editor/large does; overlap proves cross-window matches and
     * advancing past the accepted end preserves global non-overlap. */
    size_t step = remaining < FIND_POLL_UNITS * 64u ? (size_t)remaining : FIND_POLL_UNITS * 64u;
    size_t maximum = slot->query_length * FIND_MAX_OFFSETS - (slot->query_length - 1u);
    if (step > maximum) step = maximum;
    size_t overlap = slot->query_length - 1u;
    if (overlap > remaining - step) overlap = (size_t)(remaining - step);
    size_t length = step + overlap;
    _Static_assert(FIND_POLL_UNITS * 64u + FINDUI_QUERY_BYTES <= FIND_MAX_SCRATCH_BYTES, "prepaid literal window");
    piece_iter it; const uint8_t *first = NULL, *bytes; size_t n, covered = 0;
    piece_iter_begin_snapshot(&it, slot->snapshot, slot->offset);
    while (covered < length && piece_iter_next(&it, &bytes, &n)) {
        if (!first) first = bytes;
        if (!bytes || !n || (uintptr_t)bytes != (uintptr_t)first + covered) break;
        if (n > length - covered) n = length - covered;
        covered += n;
    }
    if (covered != length) {
        if (piece_snapshot_read(slot->snapshot, slot->offset, slot->scratch, length)) return FIND_ERR_ARGUMENT;
        first = slot->scratch;
    }
    if (file_snapshot_faulted(slot->snapshot)) return FIND_CANCELLED;
    find_source source = {.bytes = first, .length = length};
    find_control control = {.work = context};
    window_output output = {visit, slot->offset, slot->offset + step, slot->offset + step};
    find_visit window = {.prefix_capacity = SIZE_MAX, .wanted = FIND_UNSET, .emit = window_range, .user = &output};
    find_code code = find_literal_visit(&source, slot->query, slot->query_length, &control, &window);
    slot->offset = output.next; slot->scan_done = slot->offset == total;
    return code;
}
find_code findui_literal_step(findui_slot *slot, work_ctx *context, find_visit *visit)
{
    size_t n = slot->query_length, *failure = slot->scratch;
    uint8_t *needle = slot->pattern, *ring = needle + n;
    if (work_should_stop(context) || file_snapshot_faulted(slot->snapshot)) return FIND_CANCELLED;
    if (n == 1) return byte_step(slot, context, visit);
    if (n && slot->options.match_case && !slot->options.whole_word) return sensitive_step(slot, context, visit);
    if (!slot->offset && !slot->matched && !slot->pending) {
        size_t preprocessing = 0;
        for (size_t i = 0; i < n; i++) {
            if (poll_units(slot, context, ++preprocessing)) return FIND_CANCELLED;
            needle[i] = fold(slot->query[i], slot->options.match_case);
            if (!i) { failure[i] = 0; continue; }
            size_t k = failure[i - 1u];
            while (k && needle[k] != needle[i]) {
                if (poll_units(slot, context, ++preprocessing)) return FIND_CANCELLED;
                k = failure[k - 1u];
            }
            failure[i] = k + (needle[k] == needle[i] ? 1u : 0u);
        }
    }
    if (!n) { slot->scan_done = true; return FIND_OK; }
    uint64_t total = piece_snapshot_len(slot->snapshot);
    size_t units = 0;
    piece_iter it; const uint8_t *bytes; size_t length;
    piece_iter_begin_snapshot(&it, slot->snapshot, slot->offset);
    while (slot->offset < total && piece_iter_next(&it, &bytes, &length)) {
        for (size_t i = 0; i < length; i++) {
            if (units >= FIND_POLL_UNITS) return FIND_OK;
            if (slot->pending) {
                if (!word(bytes[i])) {
                    if (!find_visit_add(visit, visit->total, slot->candidate)) return FIND_CANCELLED;
                    visit->total++; slot->matched = 0;
                }
                slot->pending = false;
            }
            ring[slot->offset % (n + 1u)] = bytes[i];
            uint8_t byte = fold(bytes[i], slot->options.match_case);
            while (slot->matched && needle[slot->matched] != byte) {
                if (poll_units(slot, context, ++units)) return FIND_CANCELLED;
                slot->matched = failure[slot->matched - 1u];
            }
            if (needle[slot->matched] == byte) slot->matched++;
            slot->offset++; units++;
            if (poll_units(slot, context, units)) return FIND_CANCELLED;
            if (slot->matched == n) {
                slot->candidate = (find_capture){slot->offset - n, slot->offset};
                if (slot->options.whole_word) {
                    slot->pending = !slot->candidate.start || !word(ring[(slot->candidate.start - 1u) % (n + 1u)]);
                    slot->matched = failure[slot->matched - 1u];
                } else {
                    if (!find_visit_add(visit, visit->total, slot->candidate)) return FIND_CANCELLED;
                    visit->total++; slot->matched = 0;
                }
            }
        }
    }
    if (slot->pending) {
        if (!find_visit_add(visit, visit->total, slot->candidate)) return FIND_CANCELLED;
        visit->total++; slot->pending = false;
    }
    slot->scan_done = true;
    return work_should_stop(context) || file_snapshot_faulted(slot->snapshot) ? FIND_CANCELLED : FIND_OK;
}
