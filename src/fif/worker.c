#include "fif/private.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool stopped(fif_impl *impl, const work_ctx *context)
{
    if (!work_should_stop(context)) return false;
    impl->done.cancelled = true; impl->done.code = FIF_CANCELLED; return true;
}
static void io_error(fif_impl *impl)
{ impl->done.errors++; impl->done.code = FIF_ERR_IO; }
/* In-place heapsort avoids libc qsort's optional heap allocation. (P) Binary
 * heap children are 2*i+1 and 2*i+2, from breadth-first array indexing. */
static void sift(fif_node *nodes, size_t root, size_t count)
{
    while (root < count / 2u) {
        size_t child = root * 2u + 1u;
        if (child + 1u < count && strcmp(nodes[child].path, nodes[child + 1u].path) < 0) child++;
        if (strcmp(nodes[root].path, nodes[child].path) >= 0) return;
        fif_node swap = nodes[root]; nodes[root] = nodes[child]; nodes[child] = swap; root = child;
    }
}
static bool sort_nodes(fif_impl *impl, fif_node *nodes, size_t count, const work_ctx *context)
{
    for (size_t i = count / 2u; i; i--) { if (stopped(impl, context)) return false; sift(nodes, i - 1u, count); }
    for (size_t i = count; i > 1u; i--) {
        if (stopped(impl, context)) return false;
        fif_node swap = nodes[0]; nodes[0] = nodes[i - 1u]; nodes[i - 1u] = swap;
        sift(nodes, 0, i - 1u);
    }
    return true;
}
static const char *relative(const fif_impl *impl, const char *path)
{
    const char *at = path + impl->root_length;
    return *at == '/' ? at + 1 : at;
}
static bool directory(fif_impl *impl, const char *path, const work_ctx *context)
{
    if (stopped(impl, context)) return false;
    /* opendir's libc DIR allocation is confined to directory acquisition;
     * entry storage/sort/search/publication use only the prepaid base arena. */
    DIR *dir = opendir(path);
    if (!dir) { io_error(impl); return true; }
    struct stat st;
    if (fstat(dirfd(dir), &st)) { io_error(impl); (void)closedir(dir); return true; }
    for (size_t i = 0; i < impl->seen_count; i++) {
        if (stopped(impl, context)) { (void)closedir(dir); return false; }
        if (impl->seen[i].device == st.st_dev && impl->seen[i].inode == st.st_ino) {
            (void)closedir(dir); return true;
        }
    }
    impl->seen[impl->seen_count++] = (fif_identity){st.st_dev, st.st_ino};
    size_t first = impl->node_count;
    bool ok = true;
    for (;;) {
        if (stopped(impl, context)) { ok = false; break; }
        errno = 0; struct dirent *entry = readdir(dir);
        if (!entry) { if (errno) io_error(impl); break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..") || !strcmp(entry->d_name, ".git")) continue;
        if (impl->node_count == impl->limits.max_entries) {
            impl->done.truncated = true; impl->exhausted = true; ok = false; break;
        }
        size_t a = strlen(path), b = strlen(entry->d_name);
        bool slash = a && path[a - 1u] != '/';
        if (a + (slash ? 1u : 0u) + b >= FIF_PATH_BYTES) {
            impl->done.truncated = true; impl->exhausted = true; ok = false; break;
        }
        char *out = impl->nodes[impl->node_count++].path;
        memcpy(out, path, a); if (slash) out[a++] = '/';
        memcpy(out + a, entry->d_name, b + 1u);
    }
    if (closedir(dir)) io_error(impl);
    /* Never search the readdir-order-dependent prefix of an overflowing dir. */
    if (!ok) return false;
    if (!sort_nodes(impl, impl->nodes + first, impl->node_count - first, context)) return false;
    impl->frames[impl->frames_count++] = (fif_frame){first, impl->node_count - first, 0};
    return true;
}
static bool line_end(fif_impl *impl, const work_ctx *context)
{
    impl->line_end = impl->line_start;
    while (impl->line_end < impl->length && impl->bytes[impl->line_end] != '\n') {
        if (impl->line_end % FIND_POLL_UNITS == 0 && stopped(impl, context)) return false;
        impl->line_end++;
    }
    return true;
}
static bool read_file(fif_impl *impl, const char *path, const work_ctx *context)
{
    if (impl->selected == impl->limits.max_files) { impl->done.truncated = true; impl->exhausted = true; return false; }
    impl->selected++;
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) { io_error(impl); return true; }
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0) {
        io_error(impl); (void)close(fd); return true;
    }
    if ((uint64_t)st.st_size > impl->limits.max_file_bytes) {
        impl->done.truncated = true; (void)close(fd); return true;
    }
    size_t length = 0;
    bool ok = true, binary = false, probed = false;
    for (;;) {
        if (stopped(impl, context)) { ok = false; break; }
        size_t capacity = impl->limits.max_file_bytes + 1u - length;
        size_t span = capacity < FIF_POLL_BYTES ? capacity : FIF_POLL_BYTES;
        if (!probed && span > FIF_BINARY_BYTES - length) span = FIF_BINARY_BYTES - length;
        if (!span) { impl->done.truncated = true; ok = false; break; }
        ssize_t n = read(fd, impl->bytes + length, span);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { io_error(impl); ok = false; break; }
        if (!n) break;
        length += (size_t)n; impl->done.bytes += (uint64_t)n;
        if (!probed) {
            if (memchr(impl->bytes, 0, length)) { binary = true; break; }
            if (length >= FIF_BINARY_BYTES) probed = true;
        }
        if (length > impl->limits.max_file_bytes) { impl->done.truncated = true; ok = false; break; }
    }
    if (close(fd)) { io_error(impl); ok = false; }
    if (binary) { impl->done.binary++; return true; }
    if (!ok) return !impl->done.cancelled;
    if (impl->folded) {
        for (size_t i = 0; i < length; i++) {
            if (i % FIND_POLL_UNITS == 0 && stopped(impl, context)) return false;
            impl->folded[i] = find_options_fold(impl->bytes[i]);
        }
    }
    impl->length = length; impl->offset = 0; impl->line_start = 0; impl->line = 1;
    if (!line_end(impl, context)) return false;
    impl->current_file = impl->done.files++;
    fif_file_record *file = &impl->files[impl->current_file]; file->path = path;
    atomic_store_explicit(&file->ready, true, memory_order_release);
    fif_file_msg payload = {.bytes = length, .file_index = impl->current_file};
    impl->outbox = (work_msg){.kind = FIF_MSG_FILE, .generation = context->generation};
    memcpy(impl->outbox.data, &payload, sizeof payload); impl->pending = true; impl->searching = true;
    return true;
}
static bool next_file(fif_impl *impl, const work_ctx *context)
{
    while (impl->frames_count) {
        if (stopped(impl, context)) return false;
        fif_frame *frame = &impl->frames[impl->frames_count - 1u];
        if (frame->next == frame->count) { impl->frames_count--; continue; }
        const char *path = impl->nodes[frame->first + frame->next++].path;
        const char *rel = relative(impl, path);
        if (fif_filter_excludes(&impl->filter, rel, context)) continue;
        struct stat st;
        if (lstat(path, &st)) { io_error(impl); continue; }
        if (S_ISLNK(st.st_mode)) { impl->done.symlinks++; continue; }
        if (S_ISDIR(st.st_mode)) {
            if (impl->frames_count > impl->limits.max_depth) { impl->done.truncated = true; continue; }
            if (!directory(impl, path, context)) return false;
        } else if (S_ISREG(st.st_mode) && fif_filter_matches_work(&impl->filter, rel, context)) {
            if (!read_file(impl, path, context)) return false;
            if (impl->searching) return true;
        }
    }
    impl->exhausted = true; return false;
}
static bool search_next(fif_impl *impl, const work_ctx *context)
{
    find_source source = {.bytes = impl->folded ? impl->folded : impl->bytes, .length = impl->length};
    find_control control = {.work = context}; find_match match;
    find_code code = impl->options.regex ?
        find_regex_next(&source, impl->regex, impl->offset, impl->scratch, FIND_MAX_SCRATCH_BYTES, &control, &match) :
        find_literal_next(&source, impl->query, impl->query_length, impl->offset, &control, &match);
    if (code != FIND_OK) {
        if (code == FIND_CANCELLED) (void)stopped(impl, context);
        else { impl->done.code = FIF_ERR_SEARCH; impl->done.search_code = (int32_t)code; }
        return false;
    }
    if (!match.matched) { impl->searching = false; return true; }
    size_t start = (size_t)match.whole.start, end = (size_t)match.whole.end;
    bool accepted = !impl->options.whole_word ||
        ((!start || !find_options_word(impl->bytes[start - 1u])) &&
         (end == impl->length || !find_options_word(impl->bytes[end])));
    if (accepted) {
        if (impl->done.hits == impl->limits.max_hits) { impl->done.truncated = true; impl->exhausted = true; return false; }
        while (start > impl->line_end) {
            impl->line_start = impl->line_end + 1u; impl->line++;
            if (!line_end(impl, context)) return false;
        }
        uint32_t index = (uint32_t)impl->done.hits;
        fif_hit_record *hit = &impl->hits[index];
        hit->hit = (fif_hit){.line = impl->line, .column = start - impl->line_start + 1u,
            .byte_offset = start, .byte_length = end - start, .file_index = impl->current_file, .index = index};
        size_t length = impl->line_end - impl->line_start;
        if (length && impl->bytes[impl->line_start + length - 1u] == '\r') length--;
        hit->truncated = length > impl->limits.context_columns;
        hit->length = hit->truncated ? impl->limits.context_columns : length;
        uint8_t *text = impl->contexts + (size_t)index * (impl->limits.context_columns + 1u);
        /* Copy in metered chunks even when a caller raises the context cap. */
        for (size_t i = 0; i < hit->length;) {
            if (stopped(impl, context)) return false;
            size_t n = hit->length - i; if (n > FIND_POLL_UNITS) n = FIND_POLL_UNITS;
            memcpy(text + i, impl->bytes + impl->line_start + i, n); i += n;
        }
        text[hit->length] = 0; impl->done.hits++;
        atomic_store_explicit(&hit->ready, true, memory_order_release);
        impl->outbox = (work_msg){.kind = FIF_MSG_HIT, .generation = context->generation};
        memcpy(impl->outbox.data, &hit->hit, sizeof hit->hit); impl->pending = true;
    }
    if (accepted && end > start) impl->offset = end;
    else if (start < impl->length) impl->offset = start + 1u;
    else impl->searching = false;
    return true;
}
void fif_worker(work_ctx *context)
{
    fif_impl *impl = context->arg;
    if (stopped(impl, context)) { impl->search_complete = true; return; }
    if (!impl->initialized) {
        impl->initialized = true;
        if (impl->options.regex) {
            size_t error_offset = 0;
            find_code code = find_options_compile(impl->program, find_regex_bytes(), impl->query,
                impl->query_length, impl->options.match_case, impl->pattern, FIND_OPTIONS_PATTERN_BYTES,
                context, &impl->regex, &error_offset);
            if (code != FIND_OK) {
                impl->done.search_code = (int32_t)code;
                if (code == FIND_CANCELLED) (void)stopped(impl, context);
                else impl->done.code = FIF_ERR_SEARCH;
                impl->search_complete = true; return;
            }
        }
        struct stat st;
        if (lstat(impl->root, &st) || !S_ISDIR(st.st_mode)) {
            io_error(impl); impl->search_complete = true; return;
        }
        if (!directory(impl, impl->root, context)) { impl->search_complete = true; return; }
    }
    for (;;) {
        if (stopped(impl, context)) break;
        if (impl->pending) {
            if (!work_publish(context, &impl->outbox)) { (void)work_continue(context); return; }
            impl->pending = false;
        }
        if (impl->exhausted) break;
        if (impl->searching) { if (!search_next(impl, context)) break; }
        else if (!next_file(impl, context)) break;
    }
    impl->search_complete = true;
}
void fif_notify(work_ctx *context)
{
    fif_impl *impl = context->arg;
    if (!work_handle_finished(context->pool, impl->handles[0])) { (void)work_continue(context); return; }
    /* Cancellation may remove a queued search or a delivery continuation, so
     * the search function is not guaranteed a final invocation. */
    if (!impl->search_complete) {
        impl->done.cancelled = true; impl->done.code = FIF_CANCELLED;
    }
    work_msg message = {.kind = FIF_MSG_DONE, .generation = context->generation};
    memcpy(message.data, &impl->done, sizeof impl->done);
    if (!work_publish(context, &message)) (void)work_continue(context);
}
