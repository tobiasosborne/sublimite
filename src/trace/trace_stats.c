#include "trace.h"

#include <string.h>

typedef struct trace_gate_pair {
    uint32_t frame_id;
    uint64_t ns;
} trace_gate_pair;

typedef int (*trace_compare)(const void *, const void *);

static void swap(char *a, char *b, size_t width) {
    char temp[sizeof(trace_gate_pair)];
    memcpy(temp, a, width);
    memcpy(a, b, width);
    memcpy(b, temp, width);
}

/* Only the displaced root can violate the max-heap order. */
static void sift_down(char *data, size_t root, size_t n, size_t width, trace_compare cmp) {
    while (root < n / 2) {
        size_t child = root * 2 + 1;
        if (child + 1 < n && cmp(data + child * width, data + (child + 1) * width) < 0)
            child++;
        if (cmp(data + root * width, data + child * width) >= 0) return;
        swap(data + root * width, data + child * width, width);
        root = child;
    }
}

/* During extraction the prefix is a max heap and the suffix is already sorted.
 * The only elements sorted here are gate pairs and uint64_t samples. */
static void sort(void *data, size_t n, size_t width, trace_compare cmp) {
    for (size_t i = n / 2; i > 0; i--) sift_down(data, i - 1, n, width, cmp);
    for (size_t i = n; i > 1; i--) {
        swap(data, (char *)data + (i - 1) * width, width);
        sift_down(data, 0, i - 1, width, cmp);
    }
}

static int compare_pair(const void *a, const void *b) {
    const trace_gate_pair *x = a, *y = b;
    if (x->frame_id != y->frame_id) return x->frame_id < y->frame_id ? -1 : 1;
    return (x->ns > y->ns) - (x->ns < y->ns);
}

static size_t firsts(const trace_rec *recs, size_t n, enum trace_ev ev, trace_gate_pair *pairs) {
    size_t count = 0;
    for (size_t i = 0; i < n; i++) {
        if (recs[i].ev == (uint16_t)ev)
            pairs[count++] = (trace_gate_pair){recs[i].frame_id, recs[i].ns};
    }
    sort(pairs, count, sizeof pairs[0], compare_pair);
    size_t unique = 0;
    for (size_t i = 0; i < count; i++) {
        if (unique == 0 || pairs[unique - 1].frame_id != pairs[i].frame_id)
            pairs[unique++] = pairs[i];
    }
    return unique;
}

int trace_fmt_latencies(const trace_rec *recs, size_t n, enum trace_ev from,
                        enum trace_ev to, uint64_t *lat, size_t cap, size_t *nlat) {
    *nlat = 0;
    if (n == 0) return 0;
    if (n > SIZE_MAX / (2 * sizeof(trace_gate_pair))) return -1;
    size_t bytes = 2 * n * sizeof(trace_gate_pair);
    base_arena scratch;
    if (base_arena_init(&scratch, bytes) != 0) return -1;
    trace_gate_pair *a = base_arena_alloc(&scratch, bytes, _Alignof(trace_gate_pair));
    if (a == NULL) {
        base_arena_free(&scratch);
        return -1;
    }
    trace_gate_pair *b = a + n;
    size_t na = firsts(recs, n, from, a);
    size_t nb = firsts(recs, n, to, b);
    size_t used = 0;
    int result = 0;
    for (size_t i = 0, j = 0; i < na && j < nb;) {
        if (a[i].frame_id < b[j].frame_id) {
            i++;
        } else if (b[j].frame_id < a[i].frame_id) {
            j++;
        } else {
            if (b[j].ns >= a[i].ns) {
                if (used == cap) {
                    result = -1;
                    break;
                }
                lat[used++] = b[j].ns - a[i].ns;
            }
            i++;
            j++;
        }
    }
    base_arena_free(&scratch);
    if (result == 0) *nlat = used;
    return result;
}

static int compare_ns(const void *a, const void *b) {
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return (x > y) - (x < y);
}

uint64_t trace_fmt_pct(uint64_t *values, size_t n, unsigned pct) {
    if (n == 0 || pct == 0 || pct > 100) return 0;
    sort(values, n, sizeof values[0], compare_ns);
    /* ceil(n * pct / 100), expressed without overflowing the product. */
    size_t rank = n / 100u * pct + (n % 100u * pct + 99u) / 100u;
    return values[rank - 1];
}
