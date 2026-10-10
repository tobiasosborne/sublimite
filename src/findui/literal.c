/* Panel literal integration: ASCII folding retains the literal byte limit.
 * A streaming KMP state crosses piece spans; no allocations or regex atoms. */
#include "findui/private.h"
#include "find/visit.h"
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
static find_code count_byte(findui_slot *slot, work_ctx *context, find_visit *visit)
{
    piece_iter it; const uint8_t *bytes; size_t length;
    uint64_t source_length = piece_snapshot_len(slot->snapshot);
    uint64_t base = slot->window_request && slot->window_start < source_length ? slot->window_start : 0;
    uint64_t end = slot->window_request && slot->window_end < source_length ? slot->window_end : source_length;
    if (slot->window_request && (slot->window_start >= source_length || base >= end)) return FIND_OK;
    uint8_t previous = 0, needle = slot->query[0];
    bool folded = !slot->options.match_case && fold(needle, false) >= 'a' && fold(needle, false) <= 'z';
    __m128i low = _mm_set1_epi8((char)needle);
    __m128i high = _mm_set1_epi8((char)(needle >= 'a' && needle <= 'z' ? needle - 32 : needle + 32));
    if (base && slot->options.whole_word && piece_snapshot_read(slot->snapshot, base - 1, &previous, 1)) return FIND_ERR_ARGUMENT;
    piece_iter_begin_snapshot(&it, slot->snapshot, base);
    while (base < end && piece_iter_next(&it, &bytes, &length)) {
        if (length > end - base) length = (size_t)(end - base);
        size_t i = 0;
        while (length - i >= 64) {
            if ((i & 127u) == 0 && (work_should_stop(context) || file_snapshot_faulted(slot->snapshot))) return FIND_CANCELLED;
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
                else if (base + i + 64 < source_length &&
                         piece_snapshot_read(slot->snapshot, base + i + 64, &next, 1)) return FIND_ERR_ARGUMENT;
                uint64_t words = word_mask(bytes + i);
                mask &= ~((words << 1) | (word(previous) ? 1u : 0u));
                mask &= ~((words >> 1) | (word(next) ? UINT64_C(1) << 63 : 0));
            }
            if (!find_visit_mask(visit, mask, base + i, visit->total)) return FIND_CANCELLED;
            visit->total += (unsigned)__builtin_popcountll(mask);
            previous = bytes[i + 63]; i += 64;
        }
        for (; i < length; i++) {
            if ((work_should_stop(context) || file_snapshot_faulted(slot->snapshot))) return FIND_CANCELLED;
            uint8_t next = 0;
            if (i + 1 < length) next = bytes[i + 1];
            else if (base + i + 1 < source_length &&
                     piece_snapshot_read(slot->snapshot, base + i + 1, &next, 1)) return FIND_ERR_ARGUMENT;
            bool equal = fold(bytes[i], slot->options.match_case) == fold(needle, slot->options.match_case);
            if (equal && (!slot->options.whole_word || (!word(previous) && !word(next)))) {
                if (!find_visit_add(visit, visit->total, (find_capture){base + i, base + i + 1})) return FIND_CANCELLED;
                visit->total++;
            }
            previous = bytes[i];
        }
        base += length;
    }
    return (work_should_stop(context) || file_snapshot_faulted(slot->snapshot)) ? FIND_CANCELLED : FIND_OK;
}
find_code findui_literal_visit(findui_slot *slot, work_ctx *context, find_visit *visit)
{
    size_t n = slot->query_length;
    size_t *failure = slot->scratch;
    uint8_t *needle = slot->pattern;
    size_t units = 0, matched = 0;
    if ((work_should_stop(context) || file_snapshot_faulted(slot->snapshot))) return FIND_CANCELLED;
    for (size_t i = 0; i < n; i++) {
        if (++units == FIND_POLL_UNITS / 4) { units = 0; if ((work_should_stop(context) || file_snapshot_faulted(slot->snapshot))) return FIND_CANCELLED; }
        needle[i] = fold(slot->query[i], slot->options.match_case);
        if (!i) { failure[i] = 0; continue; }
        size_t k = failure[i - 1];
        while (k && needle[k] != needle[i]) {
            if (++units == FIND_POLL_UNITS / 4) { units = 0; if ((work_should_stop(context) || file_snapshot_faulted(slot->snapshot))) return FIND_CANCELLED; }
            k = failure[k - 1];
        }
        failure[i] = k + (needle[k] == needle[i] ? 1u : 0u);
    }
    if (!n) return FIND_OK;
    if (n == 1) return count_byte(slot, context, visit);
    uint8_t *ring = slot->pattern + n;
    bool pending = false;
    find_capture candidate = {0, 0};
    piece_iter it; const uint8_t *bytes; size_t length;
    uint64_t offset = 0;
    piece_iter_begin_snapshot(&it, slot->snapshot, 0);
    while (piece_iter_next(&it, &bytes, &length)) {
        for (size_t i = 0; i < length; i++, offset++) {
            if (++units == FIND_POLL_UNITS / 4) { units = 0; if ((work_should_stop(context) || file_snapshot_faulted(slot->snapshot))) return FIND_CANCELLED; }
            if (pending) {
                if (!word(bytes[i])) {
                    if (!find_visit_add(visit, visit->total, candidate)) return FIND_CANCELLED;
                    visit->total++; matched = 0;
                }
                pending = false;
            }
            ring[offset % (n + 1)] = bytes[i];
            uint8_t byte = fold(bytes[i], slot->options.match_case);
            while (matched && needle[matched] != byte) {
                if (++units == FIND_POLL_UNITS / 4) { units = 0; if ((work_should_stop(context) || file_snapshot_faulted(slot->snapshot))) return FIND_CANCELLED; }
                matched = failure[matched - 1];
            }
            if (needle[matched] == byte) matched++;
            if (matched == n) {
                candidate = (find_capture){offset + 1 - n, offset + 1};
                if (slot->options.whole_word) {
                    pending = !candidate.start || !word(ring[(candidate.start - 1) % (n + 1)]);
                    matched = failure[matched - 1];
                } else {
                    if (!find_visit_add(visit, visit->total, candidate)) return FIND_CANCELLED;
                    visit->total++; matched = 0;
                }
            }
        }
    }
    if (pending) {
        if (!find_visit_add(visit, visit->total, candidate)) return FIND_CANCELLED;
        visit->total++;
    }
    return (work_should_stop(context) || file_snapshot_faulted(slot->snapshot)) ? FIND_CANCELLED : FIND_OK;
}
